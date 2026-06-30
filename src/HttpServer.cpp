#include "../include/HttpServer.hpp"
#include "../include/HttpParser.hpp"
#include "../include/MimeTypes.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

HttpServer::HttpServer(const std::string& address, int port, size_t threadPoolSize)
    : ipAddress(address), serverPort(port), serverSocket(INVALID_SOCKET), running(false), threadPool(threadPoolSize) {
    initWinsock();
}

HttpServer::~HttpServer() {
    stopServer();
    cleanupWinsock();
}

void HttpServer::initWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        throw std::runtime_error("WSAStartup failed with error: " + std::to_string(result));
    }
}

void HttpServer::cleanupWinsock() {
    WSACleanup();
}

void HttpServer::createSocket() {
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        throw std::runtime_error("Socket creation failed with error: " + std::to_string(WSAGetLastError()));
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
}

void HttpServer::bindAndListen() {
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    
    // Bind to specific address or INADDR_ANY
    if (ipAddress == "0.0.0.0" || ipAddress.empty()) {
        serverAddr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, ipAddress.c_str(), &serverAddr.sin_addr);
    }

    int result = bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        closesocket(serverSocket);
        throw std::runtime_error("Bind failed with error: " + std::to_string(WSAGetLastError()));
    }

    result = listen(serverSocket, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        closesocket(serverSocket);
        throw std::runtime_error("Listen failed with error: " + std::to_string(WSAGetLastError()));
    }
}

void HttpServer::route(const std::string& method, const std::string& path, RouteHandler handler) {
    std::string key = method + ":" + path;
    routes[key] = handler;
}

void HttpServer::setStaticDirectory(const std::string& dirPath) {
    staticDir = fs::absolute(dirPath).string();
}

void HttpServer::start() {
    createSocket();
    bindAndListen();
    running = true;

    std::cout << "[INFO] HTTP Server started on http://" 
              << (ipAddress == "0.0.0.0" ? "localhost" : ipAddress) 
              << ":" << serverPort << std::endl;
    std::cout << "[INFO] Thread pool size: " << 4 << " threads." << std::endl;
    if (!staticDir.empty()) {
        std::cout << "[INFO] Serving static files from: " << staticDir << std::endl;
    }

    while (running) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
        
        if (clientSocket == INVALID_SOCKET) {
            if (running) {
                std::cerr << "[WARNING] Accept failed with error: " << WSAGetLastError() << std::endl;
            }
            continue;
        }

        // Delegate client handling to thread pool
        threadPool.enqueue([this, clientSocket]() {
            this->handleClient(clientSocket);
        });
    }
}

void HttpServer::stopServer() {
    if (running) {
        running = false;
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
            serverSocket = INVALID_SOCKET;
        }
        std::cout << "[INFO] Server stopped." << std::endl;
    }
}

void HttpServer::handleClient(SOCKET clientSocket) {
    std::vector<char> buffer(8192, 0);
    int bytesReceived = recv(clientSocket, buffer.data(), buffer.size() - 1, 0);

    if (bytesReceived <= 0) {
        closesocket(clientSocket);
        return;
    }

    buffer[bytesReceived] = '\0';
    std::string rawRequest(buffer.data());

    HttpRequest req = HttpParser::parse(rawRequest);
    HttpResponse res;

    // Output basic request log
    std::cout << "[REQUEST] " << req.method << " " << req.path;
    if (!req.queryParams.empty()) {
        std::cout << "?";
        bool first = true;
        for (const auto& [k, v] : req.queryParams) {
            if (!first) std::cout << "&";
            std::cout << k << "=" << v;
            first = false;
        }
    }
    std::cout << " from client socket " << clientSocket << std::endl;

    // Match route
    std::string routeKey = req.method + ":" + req.path;
    auto routeIt = routes.find(routeKey);
    if (routeIt != routes.end()) {
        try {
            res = routeIt->second(req);
        } catch (const std::exception& e) {
            res.status = 500;
            res.statusMessage = "Internal Server Error";
            res.body = "<h1>500 Internal Server Error</h1><p>" + std::string(e.what()) + "</p>";
            res.headers["Content-Type"] = "text/html";
        }
    } else {
        // Try serving static files
        if (req.method == "GET" && !staticDir.empty()) {
            res = serveStatic(req.path);
        } else {
            res.status = 404;
            res.statusMessage = "Not Found";
            res.body = "<h1>404 Not Found</h1><p>The requested route does not exist.</p>";
            res.headers["Content-Type"] = "text/html";
        }
    }

    std::string rawResponse = res.toString();
    send(clientSocket, rawResponse.c_str(), static_cast<int>(rawResponse.size()), 0);
    
    // Log response status code
    std::cout << "[RESPONSE] " << res.status << " " << res.statusMessage << " to client socket " << clientSocket << std::endl;

    closesocket(clientSocket);
}

HttpResponse HttpServer::serveStatic(const std::string& path) {
    HttpResponse res;
    
    // Sanitize path to prevent path traversal vulnerability
    fs::path baseDir = fs::canonical(staticDir);
    std::string relPath = path;
    
    // Default to index.html
    if (relPath == "/" || relPath.empty()) {
        relPath = "/index.html";
    }

    // Strip leading slash to construct relative filesystem path
    fs::path targetPath = baseDir / relPath.substr(1);
    
    try {
        // Resolve absolute canonical path
        if (!fs::exists(targetPath)) {
            res.status = 404;
            res.statusMessage = "Not Found";
            res.body = "<h1>404 Not Found</h1><p>File not found on this server.</p>";
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        fs::path canonicalTarget = fs::canonical(targetPath);

        // Security check: ensure target path is within base directory
        auto canonicalTargetStr = canonicalTarget.string();
        auto baseDirStr = baseDir.string();

        // Perform case-insensitive check on Windows
        bool isInside = true;
        if (canonicalTargetStr.size() < baseDirStr.size()) {
            isInside = false;
        } else {
            for (size_t i = 0; i < baseDirStr.size(); ++i) {
                if (std::tolower(canonicalTargetStr[i]) != std::tolower(baseDirStr[i])) {
                    isInside = false;
                    break;
                }
            }
        }

        if (!isInside) {
            res.status = 403;
            res.statusMessage = "Forbidden";
            res.body = "<h1>403 Forbidden</h1><p>Access is denied.</p>";
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        // Serve folder directories as index.html if it exists inside them
        if (fs::is_directory(canonicalTarget)) {
            fs::path indexFile = canonicalTarget / "index.html";
            if (fs::exists(indexFile)) {
                canonicalTarget = fs::canonical(indexFile);
            } else {
                res.status = 403;
                res.statusMessage = "Forbidden";
                res.body = "<h1>403 Forbidden</h1><p>Directory listing is disabled.</p>";
                res.headers["Content-Type"] = "text/html";
                return res;
            }
        }

        // Read file contents
        std::ifstream file(canonicalTarget, std::ios::binary);
        if (!file.is_open()) {
            res.status = 500;
            res.statusMessage = "Internal Server Error";
            res.body = "<h1>500 Internal Server Error</h1><p>Failed to open file.</p>";
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        res.body = buffer.str();
        res.status = 200;
        res.statusMessage = "OK";
        res.headers["Content-Type"] = MimeTypes::getType(canonicalTarget.string());
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.statusMessage = "Internal Server Error";
        res.body = "<h1>500 Internal Server Error</h1><p>" + std::string(e.what()) + "</p>";
        res.headers["Content-Type"] = "text/html";
    }

    return res;
}
