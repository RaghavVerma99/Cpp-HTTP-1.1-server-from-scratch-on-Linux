#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <ctime>

struct HttpResponse {
    int status = 200;
    std::string statusMessage = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    int64_t contentLengthHint = -1;

    static std::string dateHeader() {
        char buf[128];
        time_t now = time(nullptr);
        struct tm t{};
        gmtime_r(&now, &t);
        strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &t);
        return buf;
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << statusMessage << "\r\n";

        auto headersCopy = headers;
        if (headersCopy.find("Content-Length") == headersCopy.end()) {
            size_t length = contentLengthHint >= 0 ? static_cast<size_t>(contentLengthHint) : body.size();
            headersCopy["Content-Length"] = std::to_string(length);
        }
        if (headersCopy.find("Server") == headersCopy.end()) {
            headersCopy["Server"] = "Nexus-Custom-CPP-Server/1.0";
        }
        if (headersCopy.find("Date") == headersCopy.end()) {
            headersCopy["Date"] = dateHeader();
        }

        for (const auto& [key, value] : headersCopy) {
            oss << key << ": " << value << "\r\n";
        }
        oss << "\r\n";
        oss << body;
        return oss.str();
    }
};