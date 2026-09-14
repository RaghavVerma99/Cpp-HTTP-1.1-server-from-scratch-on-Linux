#pragma once

#include "Connection.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ThreadPool.hpp"

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <regex>
#include <map>

using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

struct RouteEntry {
    std::string method;
    std::string pattern;
    RouteHandler handler;
    std::regex regex;
    bool isRegex = false;
};

class HttpServer {
public:
    HttpServer(const std::string& address, int port, size_t threadPoolSize = 4);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void route(const std::string& method, const std::string& path, RouteHandler handler);
    void routeRegex(const std::string& method, const std::string& pattern, RouteHandler handler);
    void setStaticDirectory(const std::string& dirPath);
    void start();
    void stopServer();

    size_t workerCount() const { return threadPool.size(); }

private:
    struct CompletedResponse {
        int fd;
        std::string response;
    };

    static constexpr int kIdleTimeoutMs = 30000;

    void createSocket();
    void bindAndListen();
    void eventLoop();
    void acceptConnections();
    void readConnection(Connection& conn);
    void writeConnection(Connection& conn);
    void dispatch(Connection& conn);
    void closeConnection(int fd);
    void drainCompleted();
    void setInterest(int fd, uint32_t events);
    void checkIdleTimeouts();

    void enqueueCompleted(int fd, std::string response);

    HttpResponse handleRequest(const HttpRequest& req);
    HttpResponse serveStatic(const std::string& path);
    std::optional<std::unordered_map<std::string, std::string>> matchRoute(const std::string& method, const std::string& path, RouteEntry& entry);

    std::string ipAddress;
    int serverPort;
    int listenSocket = -1;
    int epollFd = -1;
    int eventFd = -1;
    std::atomic<bool> running{false};
    std::atomic<bool> shutdownRequested{false};
    std::string staticDir;

    ThreadPool threadPool;
    std::unordered_map<int, std::unique_ptr<Connection>> connections;
    std::vector<RouteEntry> routes;

    std::mutex completedMutex;
    std::vector<CompletedResponse> completed;
};