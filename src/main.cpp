#include "../include/HttpServer.hpp"
#include <iostream>
#include <csignal>
#include <memory>
#include <chrono>
#include <sstream>

std::unique_ptr<HttpServer> server = nullptr;

// Time of server startup for uptime calculation
auto startupTime = std::chrono::steady_clock::now();

void signalHandler(int signum) {
    std::cout << "\n[INFO] Interrupt signal (" << signum << ") received. Shutting down server..." << std::endl;
    if (server) {
        server->stopServer();
    }
}

int main(int argc, char* argv[]) {
    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int port = 8080;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "[WARNING] Invalid port argument. Defaulting to 8080." << std::endl;
        }
    }

    try {
        server = std::make_unique<HttpServer>("0.0.0.0", port, 4);

        // 1. Set static files directory
        server->setStaticDirectory("./public");

        // 2. Define custom API routes
        // GET /api/status - Server health check and metrics
        server->route("GET", "/api/status", [](const HttpRequest& req) {
            auto now = std::chrono::steady_clock::now();
            auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startupTime).count();
            double uptimeSec = uptimeMs / 1000.0;

            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";
            res.headers["Content-Type"] = "application/json";
            
            std::ostringstream json;
            json << "{\n"
                 << "  \"status\": \"healthy\",\n"
                 << "  \"uptime_seconds\": " << uptimeSec << ",\n"
                 << "  \"thread_pool_workers\": 4,\n"
                 << "  \"version\": \"1.0.0\",\n"
                 << "  \"engine\": \"Nexus C++ Engine\"\n"
                 << "}";
            res.body = json.str();
            return res;
        });

        // GET /api/greet - Greeting API parsing query parameters
        server->route("GET", "/api/greet", [](const HttpRequest& req) {
            std::string name = "Guest";
            auto it = req.queryParams.find("name");
            if (it != req.queryParams.end() && !it->second.empty()) {
                name = it->second;
            }

            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";
            res.headers["Content-Type"] = "application/json";

            std::ostringstream json;
            json << "{\n"
                 << "  \"message\": \"Hello, " << name << "! Welcome to the C++ Web Server.\",\n"
                 << "  \"query_param_received\": \"" << name << "\"\n"
                 << "}";
            res.body = json.str();
            return res;
        });

        // POST /api/echo - Echoes back the request body
        server->route("POST", "/api/echo", [](const HttpRequest& req) {
            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";
            
            // Mirror content type or default to text/plain
            auto it = req.headers.find("content-type");
            if (it != req.headers.end()) {
                res.headers["Content-Type"] = it->second;
            } else {
                res.headers["Content-Type"] = "text/plain; charset=utf-8";
            }

            if (req.body.empty()) {
                res.body = "No request body received.";
            } else {
                res.body = req.body;
            }
            return res;
        });

        // Start server (blocking loop)
        server->start();

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Server initialization failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[INFO] Clean exit." << std::endl;
    return 0;
}
