#pragma once

#include "HttpRequest.hpp"
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>

class HttpParser {
public:
    static std::string urlDecode(const std::string& str) {
        std::string decoded;
        decoded.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%') {
                if (i + 2 < str.size()) {
                    int hex = std::stoi(str.substr(i + 1, 2), nullptr, 16);
                    decoded.push_back(static_cast<char>(hex));
                    i += 2;
                }
            } else if (str[i] == '+') {
                decoded.push_back(' ');
            } else {
                decoded.push_back(str[i]);
            }
        }
        return decoded;
    }

    static HttpRequest parse(const std::string& rawRequest) {
        HttpRequest req;
        std::istringstream stream(rawRequest);
        std::string line;

        // 1. Parse Request Line
        if (std::getline(stream, line)) {
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            std::istringstream lineStream(line);
            lineStream >> req.method >> req.path >> req.version;

            // Parse Query Parameters from path
            size_t queryPos = req.path.find('?');
            if (queryPos != std::string::npos) {
                std::string queryString = req.path.substr(queryPos + 1);
                req.path = req.path.substr(0, queryPos); // strip query string from path

                std::istringstream queryStream(queryString);
                std::string pair;
                while (std::getline(queryStream, pair, '&')) {
                    size_t eqPos = pair.find('=');
                    if (eqPos != std::string::npos) {
                        std::string key = urlDecode(pair.substr(0, eqPos));
                        std::string val = urlDecode(pair.substr(eqPos + 1));
                        req.queryParams[key] = val;
                    } else if (!pair.empty()) {
                        req.queryParams[urlDecode(pair)] = "";
                    }
                }
            }
        }

        // 2. Parse Headers
        while (std::getline(stream, line)) {
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // An empty line signifies end of headers
            if (line.empty()) {
                break;
            }

            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);

                // Trim spaces
                key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                key.erase(std::find_if(key.rbegin(), key.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), key.end());

                value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), value.end());

                // Lowercase the header key to make lookup case-insensitive
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                    return std::tolower(c);
                });

                req.headers[key] = value;
            }
        }

        // 3. Parse Body (if Content-Length is present)
        auto it = req.headers.find("content-length");
        if (it != req.headers.end()) {
            try {
                size_t contentLength = std::stoull(it->second);
                
                // Read the rest of the stream up to contentLength
                std::string body;
                body.reserve(contentLength);
                
                char ch;
                while (body.size() < contentLength && stream.get(ch)) {
                    body.push_back(ch);
                }
                req.body = body;
            } catch (...) {
                // Invalid content length format, ignore body or handle gracefully
            }
        }

        return req;
    }
};
