# Build script for Antigravity C++ High-Performance HTTP Server
Write-Host "Compiling Antigravity C++ HTTP Server..." -ForegroundColor Cyan

# Compile command linking against Windows Sockets API (ws2_32)
g++ -std=c++20 -O3 -Wall src/main.cpp src/HttpServer.cpp -o server.exe -lws2_32

if ($LASTEXITCODE -eq 0) {
    Write-Host "Compilation successful! Run with: .\server.exe" -ForegroundColor Green
} else {
    Write-Host "Compilation failed." -ForegroundColor Red
}
