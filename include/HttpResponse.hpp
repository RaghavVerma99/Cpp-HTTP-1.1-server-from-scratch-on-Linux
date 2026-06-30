#pragma once

#include <string>
#include <unordered_map>
#include <sstream>

struct HttpResponse {
    int status = 200;
    std::string statusMessage = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string toString() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << statusMessage << "\r\n";
        
        // Ensure Content-Length is set if body is present
        auto headersCopy = headers;
        if (headersCopy.find("Content-Length") == headersCopy.end()) {
            headersCopy["Content-Length"] = std::to_string(body.size());
        }
        if (headersCopy.find("Server") == headersCopy.end()) {
            headersCopy["Server"] = "Nexus-Custom-CPP-Server/1.0";
        }
        
        for (const auto& [key, value] : headersCopy) {
            oss << key << ": " << value << "\r\n";
        }
        oss << "\r\n"; // Blank line separating headers and body
        oss << body;
        return oss.str();
    }
};
