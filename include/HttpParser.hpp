#pragma once

#include "HttpRequest.hpp"
#include <string>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <utility>

class HttpParser {
public:
    static std::string urlDecode(const std::string& str) {
        std::string decoded;
        decoded.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%') {
                if (i + 2 < str.size()) {
                    int hi = hexVal(str[i + 1]);
                    int lo = hexVal(str[i + 2]);
                    if (hi >= 0 && lo >= 0) {
                        decoded.push_back(static_cast<char>((hi << 4) | lo));
                        i += 2;
                    } else {
                        decoded.push_back('%');
                    }
                } else {
                    decoded.push_back('%');
                }
            } else if (str[i] == '+') {
                decoded.push_back(' ');
            } else {
                decoded.push_back(str[i]);
            }
        }
        return decoded;
    }

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    static std::string trim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
        return s;
    }

    static void normalize(std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    static bool headerLine(const std::string& line, std::string& key, std::string& value) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return false;
        }
        key = trim(line.substr(0, colon));
        value = trim(line.substr(colon + 1));
        normalize(key);
        return true;
    }

    static HttpRequest parse(const std::string& rawRequest) {
        HttpRequest req;
        std::istringstream stream(rawRequest);
        std::string line;

        if (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            std::istringstream lineStream(line);
            lineStream >> req.method >> req.path >> req.version;

            size_t queryPos = req.path.find('?');
            if (queryPos != std::string::npos) {
                std::string queryString = req.path.substr(queryPos + 1);
                req.path = req.path.substr(0, queryPos);

                std::istringstream queryStream(queryString);
                std::string pair;
                while (std::getline(queryStream, pair, '&')) {
                    size_t eqPos = pair.find('=');
                    if (eqPos != std::string::npos) {
                        req.queryParams[urlDecode(pair.substr(0, eqPos))] = urlDecode(pair.substr(eqPos + 1));
                    } else if (!pair.empty()) {
                        req.queryParams[urlDecode(pair)] = "";
                    }
                }
            }
        }

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break;
            }
            std::string key;
            std::string value;
            if (headerLine(line, key, value)) {
                req.headers[key] = value;
            }
        }

        auto contentLengthIt = req.headers.find("content-length");
        if (contentLengthIt != req.headers.end()) {
            try {
                size_t contentLength = std::stoull(contentLengthIt->second);
                char ch;
                while (req.body.size() < contentLength && stream.get(ch)) {
                    req.body.push_back(ch);
                }
            } catch (...) {
            }
        }

        return req;
    }

    static std::pair<bool, size_t> extract(const std::string& buf, HttpRequest& out,
                                           size_t maxBody = 8 * 1024 * 1024) {
        if (buf.size() < 4) {
            return {false, 0};
        }

        size_t headerEnd = buf.find("\r\n\r\n");
        size_t delimiterLen = 4;
        if (headerEnd == std::string::npos) {
            headerEnd = buf.find("\n\n");
            delimiterLen = 2;
        }
        if (headerEnd == std::string::npos) {
            return {false, 0};
        }

        size_t used = headerEnd + delimiterLen;
        std::string contentLength;
        bool chunked = false;

        std::istringstream ss(buf.substr(0, headerEnd));
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::string key;
            std::string value;
            if (!headerLine(line, key, value)) {
                continue;
            }
            if (key == "content-length") {
                contentLength = value;
            } else if (key == "transfer-encoding" && value.find("chunked") != std::string::npos) {
                chunked = true;
            }
        }

        if (chunked) {
            out = parse(buf.substr(0, used));
            return {true, used};
        }

        if (!contentLength.empty()) {
            try {
                size_t bodyLen = std::stoull(contentLength);
                if (bodyLen > maxBody) {
                    out = parse(buf.substr(0, used + bodyLen));
                    out.requestTooLarge = true;
                    return {true, used + bodyLen};
                }
                if (used + bodyLen > buf.size()) {
                    return {false, 0};
                }
                used += bodyLen;
            } catch (...) {
                out = parse(buf.substr(0, used));
                return {true, used};
            }
        }

        out = parse(buf.substr(0, used));
        return {true, used};
    }
};