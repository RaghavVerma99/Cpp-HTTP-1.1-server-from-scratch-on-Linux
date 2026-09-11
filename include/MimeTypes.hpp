#pragma once

#include <string>
#include <unordered_map>
#include <algorithm>

class MimeTypes {
public:
    static std::string getType(const std::string& path) {
        size_t dotPos = path.find_last_of('.');
        if (dotPos == std::string::npos) {
            return "application/octet-stream";
        }

        std::string ext = path.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return std::tolower(c);
        });

        static const std::unordered_map<std::string, std::string> mimeMap = {
            {"html", "text/html; charset=utf-8"},
            {"htm", "text/html; charset=utf-8"},
            {"css", "text/css; charset=utf-8"},
            {"js", "application/javascript; charset=utf-8"},
            {"mjs", "application/javascript; charset=utf-8"},
            {"json", "application/json; charset=utf-8"},
            {"txt", "text/plain; charset=utf-8"},
            {"xml", "application/xml; charset=utf-8"},
            {"svg", "image/svg+xml"},
            {"webp", "image/webp"},
            {"avif", "image/avif"},
            {"png", "image/png"},
            {"jpg", "image/jpeg"},
            {"jpeg", "image/jpeg"},
            {"gif", "image/gif"},
            {"ico", "image/x-icon"},
            {"woff", "font/woff"},
            {"woff2", "font/woff2"},
            {"ttf", "font/ttf"},
            {"otf", "font/otf"},
            {"wasm", "application/wasm"},
            {"pdf", "application/pdf"},
            {"mp4", "video/mp4"},
            {"webm", "video/webm"},
            {"mp3", "audio/mpeg"},
            {"zip", "application/zip"}
        };

        auto it = mimeMap.find(ext);
        if (it != mimeMap.end()) {
            return it->second;
        }

        return "application/octet-stream";
    }
};