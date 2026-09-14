#include "../include/HttpServer.hpp"

#include <iostream>
#include <csignal>
#include <memory>
#include <chrono>
#include <sstream>
#include <iomanip>

std::unique_ptr<HttpServer> server = nullptr;
auto startupTime = std::chrono::steady_clock::now();

static std::string jsonEscape(const std::string& input) {
    std::ostringstream escaped;
    for (char c : input) {
        switch (c) {
            case '"':  escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
                } else {
                    escaped << c;
                }
        }
    }
    return escaped.str();
}

void signalHandler(int signum) {
    (void)signum;
    if (server) {
        server->stopServer();
    }
}

int main(int argc, char* argv[]) {
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
        server->setStaticDirectory("./public");

        server->route("GET", "/api/status", [](const HttpRequest& req) {
            (void)req;
            auto now = std::chrono::steady_clock::now();
            auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startupTime).count();
            double uptimeSec = uptimeMs / 1000.0;

            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";
            res.headers["Content-Type"] = "application/json";
            res.headers["Cache-Control"] = "no-cache";

            std::ostringstream json;
            json << std::fixed << std::setprecision(1)
                 << "{\n"
                 << "  \"status\": \"healthy\",\n"
                 << "  \"uptime_seconds\": " << uptimeSec << ",\n"
                 << "  \"thread_pool_workers\": " << server->workerCount() << ",\n"
                 << "  \"version\": \"1.0.0\",\n"
                 << "  \"engine\": \"Nexus C++ Engine (Linux epoll)\"\n"
                 << "}";
            res.body = json.str();
            return res;
        });

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
                 << "  \"message\": \"Hello, " << jsonEscape(name) << "! Welcome to the C++ Web Server.\",\n"
                 << "  \"query_param_received\": \"" << jsonEscape(name) << "\"\n"
                 << "}";
            res.body = json.str();
            return res;
        });

        server->route("GET", "/api/user/:id", [](const HttpRequest& req) {
            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";
            res.headers["Content-Type"] = "application/json";

            auto it = req.routeParams.find("id");
            std::string userId = it != req.routeParams.end() ? it->second : "unknown";

            std::ostringstream json;
            json << "{\n"
                 << "  \"user_id\": \"" << jsonEscape(userId) << "\",\n"
                 << "  \"message\": \"User profile for " << jsonEscape(userId) << "\"\n"
                 << "}";
            res.body = json.str();
            return res;
        });

        server->route("GET", "/api/search", [](const HttpRequest& req) {
            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";
            res.headers["Content-Type"] = "application/json";

            std::string query = "all";
            auto it = req.queryParams.find("q");
            if (it != req.queryParams.end() && !it->second.empty()) {
                query = it->second;
            }

            std::ostringstream json;
            json << "{\n"
                 << "  \"query\": \"" << jsonEscape(query) << "\",\n"
                 << "  \"results\": []\n"
                 << "}";
            res.body = json.str();
            return res;
        });

        server->route("POST", "/api/echo", [](const HttpRequest& req) {
            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";

            auto it = req.headers.find("content-type");
            if (it != req.headers.end()) {
                res.headers["Content-Type"] = it->second;
            } else {
                res.headers["Content-Type"] = "text/plain; charset=utf-8";
            }

            res.body = req.body.empty() ? "No request body received." : req.body;
            return res;
        });

        server->routeRegex("GET", R"(/api/regex/\d+)", [](const HttpRequest& req) {
            (void)req;
            HttpResponse res;
            res.status = 200;
            res.statusMessage = "OK";
            res.headers["Content-Type"] = "application/json";
            res.body = "{\"matched\": \"numeric path\"}";
            return res;
        });

        server->start();

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Server initialization failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[INFO] Clean exit." << std::endl;
    return 0;
}