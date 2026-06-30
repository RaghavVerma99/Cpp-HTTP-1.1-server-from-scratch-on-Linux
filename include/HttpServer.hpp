#pragma once

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ThreadPool.hpp"

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <winsock2.h>
#include <ws2tcpip.h>

// Type definition for route handler functions
using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    HttpServer(const std::string& address, int port, size_t threadPoolSize = 4);
    ~HttpServer();

    // Disable copy constructors
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Registers a handler for a specific HTTP method and path
    void route(const std::string& method, const std::string& path, RouteHandler handler);

    // Set root directory for serving static files
    void setStaticDirectory(const std::string& dirPath);

    // Starts the server main listening loop (blocking)
    void start();

    // Stop the server
    void stopServer();

private:
    void initWinsock();
    void cleanupWinsock();
    void createSocket();
    void bindAndListen();
    void handleClient(SOCKET clientSocket);
    
    // Serve static files from root directory
    HttpResponse serveStatic(const std::string& path);

    std::string ipAddress;
    int serverPort;
    SOCKET serverSocket;
    bool running;
    std::string staticDir;

    ThreadPool threadPool;

    // Key format: "METHOD:PATH", e.g. "GET:/api/status"
    std::unordered_map<std::string, RouteHandler> routes;
};
