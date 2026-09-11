# Build stage: compile the C++20 server with GCC on Linux
FROM gcc:13 AS build
WORKDIR /app
COPY src ./src
COPY include ./include
RUN g++ -std=c++20 -O3 -Wall -Wextra -Iinclude src/main.cpp src/HttpServer.cpp -o nexus-server

# Runtime stage: minimal image with the binary + static files
FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /app/nexus-server .
COPY public ./public
EXPOSE 8080
ENTRYPOINT ["./nexus-server"]