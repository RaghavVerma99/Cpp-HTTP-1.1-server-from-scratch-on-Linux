#include "../include/HttpServer.hpp"
#include "../include/HttpParser.hpp"
#include "../include/MimeTypes.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstring>
#include <chrono>

#include <unistd.h>
#include <cerrno>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace fs = std::filesystem;

namespace {
constexpr int kReadBuffer = 16384;
constexpr size_t kMaxRequestBody = 8 * 1024 * 1024;

std::mutex logMutex;

void log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << msg << std::endl;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

HttpResponse errorResponse(int status, const std::string& statusMessage, const std::string& detail) {
    HttpResponse res;
    res.status = status;
    res.statusMessage = statusMessage;
    res.body = "<h1>" + std::to_string(status) + " " + statusMessage + "</h1><p>" + detail + "</p>";
    res.headers["Content-Type"] = "text/html";
    return res;
}

}  // namespace

void HttpServer::checkIdleTimeouts() {
    auto now = std::chrono::steady_clock::now();
    std::vector<int> toClose;
    for (const auto& [fd, conn] : connections) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - conn->lastActivity).count();
        if (elapsed > kIdleTimeoutMs && !conn->taskInFlight) {
            toClose.push_back(fd);
        }
    }
    for (int fd : toClose) {
        closeConnection(fd);
    }
}

HttpServer::HttpServer(const std::string& address, int port, size_t threadPoolSize)
    : ipAddress(address), serverPort(port), threadPool(threadPoolSize) {}

HttpServer::~HttpServer() {
    stopServer();
}

void HttpServer::route(const std::string& method, const std::string& path, RouteHandler handler) {
    RouteEntry entry;
    entry.method = method;
    entry.pattern = path;
    entry.handler = std::move(handler);
    routes.push_back(std::move(entry));
}

void HttpServer::setStaticDirectory(const std::string& dirPath) {
    staticDir = fs::absolute(dirPath).string();
}

void HttpServer::createSocket() {
    listenSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (listenSocket < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }

    int opt = 1;
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(listenSocket);
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed: " + std::string(std::strerror(errno)));
    }
}

void HttpServer::bindAndListen() {
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);

    if (ipAddress == "0.0.0.0" || ipAddress.empty()) {
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, ipAddress.c_str(), &serverAddr.sin_addr) != 1) {
        close(listenSocket);
        throw std::runtime_error("Invalid bind address: " + ipAddress);
    }

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        close(listenSocket);
        throw std::runtime_error("bind() failed: " + std::string(std::strerror(errno)));
    }

    if (listen(listenSocket, SOMAXCONN) < 0) {
        close(listenSocket);
        throw std::runtime_error("listen() failed: " + std::string(std::strerror(errno)));
    }
}

void HttpServer::start() {
    createSocket();
    bindAndListen();

    epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) {
        throw std::runtime_error("epoll_create1() failed: " + std::string(std::strerror(errno)));
    }

    eventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (eventFd < 0) {
        close(epollFd);
        throw std::runtime_error("eventfd() failed: " + std::string(std::strerror(errno)));
    }

    setInterest(listenSocket, EPOLLIN);
    setInterest(eventFd, EPOLLIN);

    running = true;

    log("[INFO] HTTP Server started on http://"
        + (ipAddress == "0.0.0.0" ? std::string("localhost") : ipAddress)
        + ":" + std::to_string(serverPort));
    log("[INFO] Event loop: Linux epoll + non-blocking sockets.");
    log("[INFO] Thread pool size: " + std::to_string(threadPool.size()) + " workers.");
    if (!staticDir.empty()) {
        log("[INFO] Serving static files from: " + staticDir);
    }

    eventLoop();

    log("[INFO] Draining in-flight connections...");
    threadPool.shutdown();

    if (eventFd != -1) {
        close(eventFd);
        eventFd = -1;
    }
    if (epollFd != -1) {
        close(epollFd);
        epollFd = -1;
    }
    for (const auto& [fd, conn] : connections) {
        (void)conn;
        close(fd);
    }
    connections.clear();
    if (listenSocket != -1) {
        close(listenSocket);
        listenSocket = -1;
    }

    log("[INFO] Server stopped.");
}

void HttpServer::stopServer() {
    if (!running) {
        return;
    }
    running = false;

    if (eventFd != -1) {
        uint64_t one = 1;
        ssize_t r = write(eventFd, &one, sizeof(one));
        (void)r;
    }
}

void HttpServer::setInterest(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) < 0 && errno == EEXIST) {
        epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev);
    }
}

void HttpServer::eventLoop() {
    std::vector<epoll_event> events(64);

    while (running) {
        checkIdleTimeouts();

        int n = epoll_wait(epollFd, events.data(), static_cast<int>(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[ERROR] epoll_wait failed: " << std::strerror(errno) << std::endl;
            break;
        }
        if (n == 0) {
            continue;
        }
        if (static_cast<size_t>(n) == events.size()) {
            events.resize(events.size() * 2);
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listenSocket) {
                acceptConnections();
            } else if (fd == eventFd) {
                uint64_t count;
                ssize_t r = read(eventFd, &count, sizeof(count));
                (void)r;
                drainCompleted();
            } else {
                auto it = connections.find(fd);
                if (it == connections.end()) {
                    close(fd);
                    continue;
                }
                Connection* conn = it->second.get();

                if ((ev & (EPOLLERR | EPOLLHUP)) && !(ev & EPOLLIN)) {
                    if (conn->taskInFlight) {
                        conn->abandoned = true;
                        epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
                    } else {
                        closeConnection(fd);
                    }
                    continue;
                }
                if (ev & EPOLLIN) {
                    readConnection(*conn);
                    it = connections.find(fd);
                    if (it == connections.end()) {
                        continue;
                    }
                    conn = it->second.get();
                }
                if (ev & EPOLLOUT) {
                    it = connections.find(fd);
                    if (it == connections.end()) {
                        continue;
                    }
                    writeConnection(*it->second.get());
                }
            }
        }
    }
}

void HttpServer::acceptConnections() {
    while (true) {
        int client = accept4(listenSocket, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[WARNING] accept() failed: " << std::strerror(errno) << std::endl;
            break;
        }

        int one = 1;
        if (setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
            std::cerr << "[WARNING] setsockopt(TCP_NODELAY) failed: " << std::strerror(errno) << std::endl;
        }

        connections[client] = std::make_unique<Connection>(client);
        connections[client]->lastActivity = std::chrono::steady_clock::now();
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = client;
        epoll_ctl(epollFd, EPOLL_CTL_ADD, client, &ev);
    }
}

void HttpServer::readConnection(Connection& conn) {
    char buffer[kReadBuffer];
    bool eof = false;

    while (true) {
        ssize_t n = recv(conn.fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            conn.inBuf.append(buffer, static_cast<size_t>(n));
            conn.lastActivity = std::chrono::steady_clock::now();
            if (conn.inBuf.size() > kMaxRequestBody + 16 * 1024) {
                closeConnection(conn.fd);
                return;
            }
            continue;
        }
        if (n == 0) {
            eof = true;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        eof = true;
        break;
    }

    if (!conn.taskInFlight) {
        dispatch(conn);
    }

    if (eof) {
        if (conn.taskInFlight) {
            conn.peerClosed = true;
            epoll_event ev{};
            ev.events = EPOLLOUT;
            ev.data.fd = conn.fd;
            epoll_ctl(epollFd, EPOLL_CTL_MOD, conn.fd, &ev);
        } else {
            closeConnection(conn.fd);
        }
    }
}

void HttpServer::dispatch(Connection& conn) {
    HttpRequest req;
    auto [complete, used] = HttpParser::extract(conn.inBuf, req, kMaxRequestBody);
    if (!complete) {
        return;
    }

    std::string connection;
    auto connIt = req.headers.find("connection");
    if (connIt != req.headers.end()) {
        connection = toLower(connIt->second);
    }

    if (req.version == "HTTP/1.1") {
        conn.keepAlive = connection.find("close") == std::string::npos;
    } else {
        conn.keepAlive = connection.find("keep-alive") != std::string::npos;
    }

    if (req.headers.count("transfer-encoding") > 0 || req.requestTooLarge) {
        conn.keepAlive = false;
    }

    conn.inBuf.erase(0, used);
    conn.taskInFlight = true;

    int fd = conn.fd;
    bool keepAlive = conn.keepAlive;
    threadPool.enqueue([this, fd, keepAlive, req = std::move(req)]() {
        HttpResponse res = handleRequest(req);
        res.headers["Connection"] = keepAlive ? "keep-alive" : "close";
        enqueueCompleted(fd, res.toString());
    });
}

void HttpServer::writeConnection(Connection& conn) {
    while (conn.outOffset < conn.outBuf.size()) {
        ssize_t n = send(conn.fd,
                         conn.outBuf.data() + conn.outOffset,
                         conn.outBuf.size() - conn.outOffset,
                         MSG_NOSIGNAL);
        if (n > 0) {
            conn.outOffset += static_cast<size_t>(n);
            conn.lastActivity = std::chrono::steady_clock::now();
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        closeConnection(conn.fd);
        return;
    }

    if (conn.outBuf.empty()) {
        return;
    }

    conn.outBuf.clear();
    conn.outOffset = 0;

    if (conn.peerClosed || !conn.keepAlive) {
        closeConnection(conn.fd);
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = conn.fd;
    epoll_ctl(epollFd, EPOLL_CTL_MOD, conn.fd, &ev);

    dispatch(conn);
}

void HttpServer::drainCompleted() {
    std::vector<CompletedResponse> items;
    {
        std::lock_guard<std::mutex> lock(completedMutex);
        items.swap(completed);
    }

    for (auto& item : items) {
        auto it = connections.find(item.fd);
        if (it == connections.end()) {
            continue;
        }
        Connection& conn = *it->second;
        conn.taskInFlight = false;
        conn.lastActivity = std::chrono::steady_clock::now();

        if (conn.abandoned) {
            closeConnection(item.fd);
            continue;
        }

        conn.outBuf = std::move(item.response);
        conn.outOffset = 0;

        epoll_event ev{};
        ev.events = EPOLLOUT;
        ev.data.fd = conn.fd;
        epoll_ctl(epollFd, EPOLL_CTL_MOD, conn.fd, &ev);
    }
}

void HttpServer::closeConnection(int fd) {
    auto it = connections.find(fd);
    if (it == connections.end()) {
        return;
    }
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections.erase(it);
}

void HttpServer::enqueueCompleted(int fd, std::string response) {
    {
        std::lock_guard<std::mutex> lock(completedMutex);
        completed.push_back({fd, std::move(response)});
    }
    if (eventFd != -1) {
        uint64_t one = 1;
        ssize_t r = write(eventFd, &one, sizeof(one));
        (void)r;
    }
}

HttpResponse HttpServer::handleRequest(const HttpRequest& req) {
    HttpResponse res;

    if (req.path.empty() || req.method.empty()) {
        return errorResponse(400, "Bad Request", "The request could not be parsed.");
    }

    if (req.requestTooLarge) {
        return errorResponse(413, "Payload Too Large", "The request body exceeds the server limit.");
    }

    if (req.headers.count("transfer-encoding") > 0) {
        return errorResponse(501, "Not Implemented", "Transfer-Encoding is not supported.");
    }

    for (auto& entry : routes) {
        if (entry.method == req.method && entry.pattern == req.path) {
            try {
                res = entry.handler(req);
            } catch (const std::exception& e) {
                return errorResponse(500, "Internal Server Error", std::string(e.what()));
            }
            goto found;
        }
    }
    if ((req.method == "GET" || req.method == "HEAD") && !staticDir.empty()) {
        res = serveStatic(req.path);
        if (req.method == "HEAD") {
            res.contentLengthHint = static_cast<int64_t>(res.body.size());
            res.body.clear();
        }
    } else {
        return errorResponse(404, "Not Found", "The requested route does not exist.");
    }
found:
    log("[RESPONSE] " + std::to_string(res.status) + " " + res.statusMessage
        + " for " + req.method + " " + req.path);

    return res;
}

HttpResponse HttpServer::serveStatic(const std::string& path) {
    fs::path baseDir;
    try {
        baseDir = fs::canonical(fs::absolute(staticDir));
    } catch (const std::exception&) {
        return errorResponse(500, "Internal Server Error", "Static directory unavailable.");
    }

    std::string relPath = path;
    if (relPath == "/" || relPath.empty()) {
        relPath = "/index.html";
    }

    fs::path targetPath = baseDir / relPath.substr(1);

    try {
        if (!fs::exists(targetPath) || !fs::is_regular_file(targetPath)) {
            return errorResponse(404, "Not Found", "File not found on this server.");
        }

        fs::path canonicalTarget = fs::canonical(targetPath);

        bool inside = true;
        for (const auto& component : fs::relative(canonicalTarget, baseDir)) {
            if (component == "..") {
                inside = false;
                break;
            }
        }
        if (canonicalTarget == baseDir) {
            inside = false;
        }

        if (!inside) {
            return errorResponse(403, "Forbidden", "Access is denied.");
        }

        std::ifstream file(canonicalTarget, std::ios::binary);
        if (!file.is_open()) {
            return errorResponse(500, "Internal Server Error", "Failed to open file.");
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        HttpResponse res;
        res.body = buffer.str();
        res.status = 200;
        res.statusMessage = "OK";
        res.contentLengthHint = static_cast<int64_t>(res.body.size());
        res.headers["Content-Type"] = MimeTypes::getType(canonicalTarget.string());
        return res;
    } catch (const std::exception& e) {
        return errorResponse(500, "Internal Server Error", std::string(e.what()));
    }
}