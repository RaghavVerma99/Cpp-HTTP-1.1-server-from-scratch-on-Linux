# ⚡ Nexus HTTP Server

> A dependency-free HTTP/1.1 web server written in C++20 from scratch.
> Built entirely on Linux kernel primitives — `epoll`, `eventfd`, and non-blocking POSIX sockets.
> No frameworks. No external libraries. Just the kernel and the C++ standard library.

---

## 📑 Table of Contents

1. [What Is This?](#what-is-this)
2. [What It Does](#what-it-does)
3. [Why This Exists](#why-this-exists)
4. [How It Works](#how-it-works)
5. [Architecture Overview](#architecture-overview)
6. [The Request Lifecycle](#the-request-lifecycle)
7. [Core Concepts Explained](#core-concepts-explained)
8. [Per-Connection State](#per-connection-state)
9. [Building](#building)
10. [Running](#running)
11. [API Reference](#api-reference)
12. [How to Add Your Own Routes](#how-to-add-your-own-routes)
13. [Project Layout](#project-layout)
14. [Known Limitations](#known-limitations)
15. [What Was Fixed](#what-was-fixed)

---

## 🧭 What Is This?

Nexus is a **complete HTTP/1.1 web server** written entirely in C++20.
If you've ever wondered what happens behind the scenes when you type `http://localhost:8080` in your browser, this project shows you every single step.

Instead of using a ready-made framework like Boost.Asio or libevent, Nexus talks directly to the Linux kernel. It uses:

- **`epoll`** to watch thousands of connections at once with minimal overhead
- **`eventfd`** as a safe signal channel between worker threads and the main loop
- **Non-blocking sockets** so no single slow client can stall the entire server
- **A thread pool** to handle CPU-heavy work like parsing and file reads without blocking the event loop

**In one sentence:** one thread watches all the doors (the event loop), a small team of workers handles the hard stuff (parsing, routing, file I/O), and everything communicates through safe, kernel-managed channels.

---

## 📌 What It Does

When a client (browser, `curl`, any HTTP tool) connects to Nexus, here's exactly what happens:

```
Client sends request
       ↓
  Accept the connection
       ↓
  Read the raw bytes from the socket
       ↓
  Parse the HTTP request (method, path, headers, body)
       ↓
  Look up a matching route handler
       ↓
  Run the handler (or serve a static file)
       ↓
  Build a proper HTTP response
       ↓
  Send the response back to the client
       ↓
  Keep the connection open OR close it
```

It handles the messy parts of real-world HTTP too:

| Scenario | What Nexus Does |
| --- | --- |
| Bare `\n` line endings (not `\r\n`) | Handles it gracefully |
| Multiple requests on one connection (pipelining) | Processes them in order |
| Request body too large | Returns `413 Payload Too Large` immediately |
| `HEAD` request (no body needed) | Returns headers only, no body sent |
| Path traversal attempt (`../`) | Returns `403 Forbidden` |
| Unknown route | Returns `404 Not Found` |
| Idle keep-alive connection | Closes after 30 seconds of no activity |

---

## 🎯 Why This Exists

Three design goals shaped every decision:

| Goal | What It Means in Practice |
| --- | --- |
| **Serve many connections with few threads** | One thread watches all sockets. No thread-per-connection model wasting 8 MB of stack memory per idle client. |
| **Never block the main loop** | Parsing, routing, and reading files happen on worker threads. The main loop only does fast I/O (`recv`, `send`). |
| **Be readable and debuggable** | Every line is standard C++20 and Linux syscalls. You can step through it in GDB without jumping through abstraction layers. |

**The old way vs. Nexus:**

```
Traditional beginner server:
  Client 1 → handle → Client 2 → handle → Client 3 → handle
  (one at a time, everyone waits)

Nexus:
  [Event Loop Thread] ← watches ALL clients simultaneously
       ↓
  [Worker 1]  ← handles request A
  [Worker 2]  ← handles request B
  [Worker 3]  ← handles request C
  [Worker 4]  ← handles request D
  (everyone served at the same time)
```

---

## 🏗️ Architecture Overview

Nexus is split into two halves that communicate through a shared task queue.

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLIENTS (browsers, curl, etc.)           │
└──────────────────────────┬──────────────────────────────────────┘
                           │ sockets
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│              THE I/O PLANE (1 event loop thread)                │
│                                                                 │
│   accept4()  →  recv()  →  HttpParser::extract()  →  dispatch() │
│                                                                 │
│   When a request is ready:                                      │
│     → send it to a worker thread via the task queue             │
│                                                                 │
│   When a response arrives:                                      │
│     → write it back to the socket                               │
│     → keep the connection open (if keep-alive)                  │
│                                                                 │
└──────────────────────┬──────────────────────────────────────────┘
                       │ task queue (std::queue + mutex)
                       │ eventfd (wake-up signal)
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│            THE COMPUTE PLANE (4 worker threads)                  │
│                                                                 │
│   Worker 1:  parse(req) → route handler → serialize response   │
│   Worker 2:  parse(req) → static file → serialize response     │
│   Worker 3:  parse(req) → route handler → serialize response   │
│   Worker 4:  parse(req) → static file → serialize response     │
│                                                                 │
│   When done: write(eventfd, 1) → wakes the event loop          │
└─────────────────────────────────────────────────────────────────┘
```

### The Three Pillars

#### 1. `epoll` — The Multiplexer

Think of `epoll` as a security guard watching 100 doors at once. Instead of walking to each door every second to check if someone's there (which wastes time), the guard stands still and only moves when the doorbell rings.

```cpp
// The loop waits for ANY socket to have data ready
int n = epoll_wait(epollFd, events.data(), size, -1);
// n = 0 means "nothing happened, keep waiting"
// n > 0 means "these sockets need attention"
```

This is why one thread can handle thousands of connections — it never wastes time checking sockets that have nothing to say.

#### 2. `eventfd` — The Ping Between Threads

Worker threads live on their own threads and can't safely touch the event loop's data structures. So instead of sharing memory, they send a tiny signal:

```cpp
// Worker: "Hey, I'm done!"
uint64_t one = 1;
write(eventFd, &one, sizeof(one));

// Event loop: wakes up, drains completed responses
read(eventFd, &count, sizeof(count));
```

It's like a doorbell — the worker pushes the button, the loop comes to collect.

#### 3. The Thread Pool — Bounded Parallelism

A fixed number of worker threads (default: 4) ensures that if 100 clients connect at once, only 4 requests are processed at a time. This prevents memory explosion and keeps the system predictable.

---

## 🔄 The Request Lifecycle

Here's what happens from the moment a client connects to when they get a response:

```
  Client                                          Nexus Server
   │                                                    │
   │──── TCP SYN ──────────────────────────────────────►│
   │                                                    │ accept4()
   │◄─── TCP SYN-ACK ─────────────────────────────────│
   │                                                    │ EPOLLIN registered
   │──── HTTP GET /api/status ────────────────────────►│
   │                                                    │ recv() → inBuf
   │                                                    │ HttpParser::extract()
   │                                                    │ → Request parsed!
   │                                                    │ dispatch() → task queue
   │                                                    │ Worker picks it up
   │                                                    │ → route handler runs
   │                                                    │ → HttpResponse created
   │                                                    │ serialize to string
   │                                                    │ write(eventfd, 1)
   │                                                    │
   │◄─── HTTP/1.1 200 OK ─────────────────────────────│
   │                                                    │ send(outBuf)
   │                                                    │
   │──── (connection stays open for next request)       │
```

### Step-by-step breakdown:

**Step 1 — The Listener**

One non-blocking socket sits and waits for incoming connections.

```cpp
listenSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
bind(listenSocket, ...);
listen(listenSocket, SOMAXCONN);
```

`SO_REUSEADDR` means if you restart the server quickly, you won't get "Address already in use" errors.

**Step 2 — The Event Loop**

The loop sits idle until the kernel says something needs attention:

```cpp
int n = epoll_wait(epollFd, events.data(), events.size(), -1);
```

Three types of events can fire:
- **Listen socket has a new connection** → `acceptConnections()`
- **Worker sent a completed response** → `drainCompleted()`
- **A client socket has data to read** → `readConnection()`

**Step 3 — Incremental Parsing**

The parser reads the raw bytes and finds the boundary between headers and body:

```
GET /api/status HTTP/1.1\r\n
Host: localhost:8080\r\n
Connection: keep-alive\r\n
\r\n
<body (if present)>
```

- If the headers are complete but the body hasn't arrived → **wait for more data**
- If the body is too large → **return 413 immediately**
- If everything is here → **return the full request**

**Step 4 — Dispatch to Workers**

The event loop hands the request to a worker thread and goes back to watching sockets:

```cpp
threadPool.enqueue([this, fd, keepAlive, req = std::move(req)]() {
    HttpResponse res = handleRequest(req);
    enqueueCompleted(fd, res.toString());
});
```

The loop is now free to serve other clients while the worker does the parsing and routing.

**Step 5 — Write the Response**

When the worker finishes, it signals the loop via `eventfd`. The loop then writes the serialized HTTP response back to the socket.

**Step 6 — Keep-Alive or Close**

If the client said `Connection: keep-alive`, the loop re-arms the socket for more requests. Otherwise, it closes the connection cleanly.

---

## 🧠 Core Concepts Explained Simply

### Non-Blocking Sockets

Normally, `recv()` would freeze your program until data arrives. With non-blocking sockets, `recv()` returns immediately — either with data, or with an error saying "nothing here yet." This lets the loop check dozens of sockets in a row without ever getting stuck.

```
Blocking:   recv() ──→ [waits forever] ──→ data!
Non-blocking: recv() ──→ "EAGAIN, try again later" ──→ move on to next socket
```

### Level-Triggered vs Edge-Triggered epoll

Nexus uses **level-triggered** epoll. This means: "as long as there's data in the socket buffer, keep telling me." It's simpler and more forgiving — if you miss a notification, you'll get another one next time you call `epoll_wait`.

### Connection State Machine

Each connection goes through these states:

```
┌──────────┐
│  CONNECT  │  ← Client just connected
└────┬─────┘
     │ recv() got data
     ▼
┌──────────┐
│  PARSING  │  ← Waiting for complete request
└────┬─────┘
     │ Request is complete
     ▼
┌──────────┐
│  WORKING  │  ← Worker is processing
└────┬─────┘
     │ Worker finished
     ▼
┌──────────┐
│ SENDING  │  ← Writing response to socket
└────┬─────┘
     │ Response fully sent
     ▼
┌──────────┐
│  DONE    │  → Keep-alive? → back to PARSING
│          │  → Close? → close the socket
└──────────┘
```

### The `taskInFlight` Guard

An fd (file descriptor) must never be closed while a worker thread is still using it. If a client disconnects mid-request and the loop immediately closes that fd, the OS could hand the same fd number to a brand-new connection. The worker's finished response would then accidentally be written into the new connection's stream.

`taskInFlight` prevents this: the loop marks the connection busy while a worker is processing, and won't close it until the worker is done.

---

## 📊 Per-Connection State

Every accepted socket carries a small `Connection` struct that tracks everything about that client:

| Field | Type | What It Does |
| --- | --- | --- |
| `fd` | `int` | The socket file descriptor |
| `inBuf` | `string` | Bytes received but not yet parsed into a full request |
| `outBuf` | `string` | The serialized response waiting to be sent |
| `outOffset` | `size_t` | How many bytes of `outBuf` have already been sent |
| `keepAlive` | `bool` | Whether to watch for a next request after this response |
| `taskInFlight` | `bool` | True while a worker is processing this connection |
| `peerClosed` | `bool` | The client sent a FIN (they're done) — deliver response then close |
| `abandoned` | `bool` | The connection died mid-request — defer close until response is drained |
| `lastActivity` | `time_point` | When the client last sent data or received data (for idle timeout) |

---

## 🛠️ Building

### Prerequisites

- **Linux** (the server uses Linux-specific syscalls: `epoll`, `eventfd`, `accept4`)
- **C++20 compiler** (GCC ≥ 11 or Clang ≥ 14)
- **`make`** or **CMake ≥ 3.16**
- **`pthread`** (linked automatically)

### Build Options

**Using Make (simplest):**
```bash
make              # Compiles → ./nexus-server
make run          # Compiles and runs on port 8080
make clean        # Removes the binary
```

**Using CMake:**
```bash
cmake -B build -S .
cmake --build build -j
```

**Direct compilation (for debugging):**
```bash
g++ -std=c++20 -O3 -Wall -Wextra -Iinclude src/main.cpp src/HttpServer.cpp -o nexus-server
```

### Docker Build
```bash
docker build -t nexus-server .
docker run -p 8080:8080 nexus-server
```

---

## ▶️ Running

```bash
./nexus-server [port]     # port defaults to 8080
```

| Signal | What Happens |
| --- | --- |
| `Ctrl+C` (SIGINT) | Server stops accepting new connections, finishes in-flight requests, closes everything gracefully, exits cleanly |
| `kill` (SIGTERM) | Same graceful shutdown as above |

The server prints startup info to the console:
```
[INFO] HTTP Server started on http://localhost:8080
[INFO] Event loop: Linux epoll + non-blocking sockets.
[INFO] Thread pool size: 4 workers.
[INFO] Serving static files from: ./public
```

Open your browser to `http://localhost:8080` and you'll see the live dashboard with metrics, a benchmark tool, and an API playground.

---

## 📡 API Reference

### Built-in Endpoints

| Endpoint | Method | Description | Example Response |
| --- | --- | --- | --- |
| `/` | `GET` / `HEAD` | Serves the dashboard HTML page | Full HTML page |
| `/api/status` | `GET` | Health check — uptime, worker count, version | `{"status":"healthy","uptime_seconds":12.4,"thread_pool_workers":4}` |
| `/api/greet?name=X` | `GET` | Personalized greeting | `{"message":"Hello, X! Welcome..."}` |
| `/api/echo` | `POST` | Echoes back whatever body you send | Whatever you posted |

### Try it with curl:

```bash
# Health check
curl http://localhost:8080/api/status

# Personalized greeting
curl 'http://localhost:8080/api/greet?name=Raghav'

# POST echo
curl -X POST -d '{"key":"value"}' http://localhost:8080/api/echo

# Static file (dashboard)
curl http://localhost:8080/
```

### Error Codes

| Code | Meaning | When It Happens |
| --- | --- | --- |
| `400` | Bad Request | Request couldn't be parsed |
| `403` | Forbidden | Path tried to escape the static root |
| `404` | Not Found | Route doesn't exist and no static file matches |
| `413` | Payload Too Large | Request body exceeds 8 MB limit |
| `500` | Internal Server Error | Handler threw an exception |
| `501` | Not Implemented | Transfer-Encoding is not supported |

---

## 🛠️ How to Add Your Own Routes

### Simple Exact Match Route

```cpp
server->route("GET", "/api/hello", [](const HttpRequest& req) {
    HttpResponse res;
    res.status = 200;
    res.headers["Content-Type"] = "text/plain";
    res.body = "Hello, world!";
    return res;
});
```

### Query Parameters

Query strings are automatically parsed and available in `req.queryParams`:

```cpp
server->route("GET", "/api/greet", [](const HttpRequest& req) {
    std::string name = "Guest";
    auto it = req.queryParams.find("name");
    if (it != req.queryParams.end()) {
        name = it->second;
    }
    HttpResponse res;
    res.body = "Hello, " + name;
    return res;
});
// /api/greet?name=Raghav → "Hello, Raghav"
```

### POST Requests

The request body is available in `req.body`:

```cpp
server->route("POST", "/api/echo", [](const HttpRequest& req) {
    HttpResponse res;
    res.body = req.body;  // Echo back whatever was sent
    return res;
});
```

### JSON Safety

Always escape user input before putting it in JSON:

```cpp
std::string jsonEscape(const std::string& input);  // Provided in main.cpp
// Prevents injection: <script>alert(1)</script> becomes a safe string
```

---

## 📁 Project Layout

```
HTTP-Server-in-C/
├── include/
│   ├── Connection.hpp      # Per-connection state: buffers, keep-alive, idle timeout
│   ├── HttpParser.hpp      # Parses raw bytes into HTTP requests, URL decoding
│   ├── HttpRequest.hpp     # Request model: method, path, headers, body, query params, route params
│   ├── HttpResponse.hpp    # Response model: status, headers, body, automatic Content-Length
│   ├── HttpServer.hpp      # Main server class: event loop, routing, thread pool
│   ├── MimeTypes.hpp       # File extension → Content-Type mapping
│   └── ThreadPool.hpp      # Fixed-size worker thread pool with task queue
├── src/
│   ├── HttpServer.cpp      # Socket setup, epoll loop, dispatch, static file serving, route matching
│   └── main.cpp            # Routes, signal handlers, JSON escaping, entry point
├── public/
│   └── index.html          # Live dashboard with metrics, benchmark tool, and API playground
├── CMakeLists.txt           # CMake build configuration
├── Dockerfile               # Multi-stage Docker build
├── Makefile                 # make / make run / make clean
└── nexus-server             # Compiled binary (generated)
```

### File Responsibilities

| File | Role |
| --- | --- |
| `HttpServer.hpp/cpp` | The brain — creates sockets, runs the event loop, dispatches to workers |
| `HttpParser.hpp` | The translator — turns raw bytes into structured request data |
| `HttpRequest.hpp` | The request model — holds method, path, headers, body, params |
| `HttpResponse.hpp` | The response model — serializes to proper HTTP wire format |
| `Connection.hpp` | The state tracker — per-client buffers, flags, timestamps |
| `ThreadPool.hpp` | The task distributor — manages worker threads and the queue |
| `MimeTypes.hpp` | The file type detector — maps `.html`, `.css`, `.js` to proper Content-Type |
| `main.cpp` | The setup — registers routes, handles signals, starts the server |

---

## ✅ Bug Fixes Applied

The following bugs were identified and fixed during the audit:

| Bug | Problem | Fix |
| --- | --- | --- |
| **Buffer leak on oversized requests** | When `Content-Length` exceeded the limit, the body bytes were never consumed from the read buffer, causing stale data to accumulate | Body bytes are now correctly consumed even when rejecting oversized requests |
| **Silent `setsockopt` failures** | `SO_REUSEADDR` and `TCP_NODELAY` errors were ignored, leading to subtle socket issues | Added proper error checking with exceptions and warnings |
| **`EPOLL_CTL_ADD` on already-registered fd** | Could fail with `EEXIST` in edge cases | Added fallback to `EPOLL_CTL_MOD` |
| **No idle timeout** | Keep-alive clients that never send another request hold a file descriptor forever | Added 30-second idle timeout that automatically closes stale connections |
| **Fixed-size events buffer** | If more than 64 events fired in one `epoll_wait` call, some were silently dropped | Dynamic resizing of the events buffer |
| **No graceful shutdown** | `stopServer()` closed all connections immediately, potentially dropping in-flight responses | Server now drains completed responses before closing everything |
| **Missing `Content-Length` for static files** | Static file responses didn't set an explicit content length hint | Added `contentLengthHint` for accurate headers |

---

## 🆕 New Features Added

| Feature | Description |
| --- | --- |
| **Idle connection timeout** | 30-second inactivity timeout prevents file descriptor leaks |
| **Dynamic event buffer** | `epoll_wait` buffer grows automatically under heavy load |
| **Graceful shutdown** | Drains in-flight responses before exiting |

---

## 🔮 Known Limitations

These are honest gaps in the current implementation — areas that could be improved:

- **No TLS/HTTPS** — Traffic is plain HTTP/1.1. HTTPS should sit behind a reverse proxy like Nginx or Caddy.
- **Whole files buffered in memory** — Large static files are read entirely into memory. A production server would use `sendfile()` or similar zero-copy techniques.
- **No config file** — Port, workers, routes, and static root are all hardcoded in `src/main.cpp`. A JSON or YAML config file would make deployment easier.
- **No automated tests** — The parser has edge cases (bare `\n`, chunked encoding, giant `Content-Length`) that would benefit from a unit test suite.
- **No compression** — No gzip or brotli compression for responses.
- **No WebSocket support** — Only HTTP/1.1 is implemented.
- **No IPv6** — Only IPv4 addresses are supported.
- **No rate limiting** — Any client can send unlimited requests.

---

## 🚀 What's Next?

Ideas for future development:

1. **Config file support** — Load routes, ports, and settings from a JSON/YAML file
2. **Unit tests** — Test the parser, router, and connection handling
3. **Access logging** — Log every request to a file with timestamps and status codes
4. **Rate limiting** — Limit requests per IP address per time window
5. **Compression** — Add gzip/brotli support for responses
6. **TLS/HTTPS** — Integrate OpenSSL or mbedTLS
7. **WebSocket upgrade** — Support WebSocket handshakes and framing
8. **Reverse proxy** — Forward requests to backend services
9. **IPv6 support** — Dual-stack IPv4/IPv6 listening
10. **Performance profiling** — Add metrics for requests per second, average latency, active connections

---

*Nexus HTTP Server — C++20, Linux epoll, zero dependencies. Built from the ground up, nothing hidden.*
