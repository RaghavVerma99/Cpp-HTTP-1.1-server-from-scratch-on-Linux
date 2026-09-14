#pragma once

#include <string>
#include <chrono>

struct Connection {
    explicit Connection(int fd) : fd(fd) {}

    int fd = -1;
    std::string inBuf;
    std::string outBuf;
    size_t outOffset = 0;
    bool keepAlive = false;
    bool taskInFlight = false;
    bool peerClosed = false;
    bool abandoned = false;
    std::chrono::steady_clock::time_point lastActivity = std::chrono::steady_clock::now();
};