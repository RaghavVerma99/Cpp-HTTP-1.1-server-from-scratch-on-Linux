# ⚡ Nexus HTTP Server

> A simple HTTP web server written in C++20, with **no external libraries**.
> It is small, understandable, and a great way to see what a web server
> actually does under the hood.

---

## 📑 Table of Contents

- [What Is This?](#-what-is-this)
- [How a Web Server Works (The Big Picture)](#-how-a-web-server-works-the-big-picture)
- [How Nexus Works, Step by Step](#-how-nexus-works-step-by-step)
- [Features](#-features)
- [Build](#-build)
- [Run](#-run)
- [The Server's Endpoints (API)](#-the-servers-endpoints-api)
- [Try It Yourself!](#-try-it-yourself)
- [Project Layout](#-project-layout)
- [What Nexus Does Not Do (Yet)](#-what-nexus-does-not-do-yet)
- [A Note on Putting This Online](#-a-note-on-putting-this-online)

---

## 🌱 What Is This?

Nexus is a **web server** written by hand in C++.

When you type a web address into your browser, your browser sends a
**request** to a server. The server looks at the request, figures out
what you want (a web page? some data? a file?), and sends back a
**response**. That request-and-response dance is the whole job of a web server.

Nexus does that job using only the tools that C++ and Linux give you —
no fancy frameworks, no downloaded packages. Every line of code is right
here in this project, so you can read all of it.

---

## 🖼️ How a Web Server Works (The Big Picture)

Think of a web server like a **restaurant**.

| Role | Restaurant | Web server |
| --- | --- | --- |
| The building's front door | guests walk in | the server *listens* on a port |
| The host | a guest arrives → greet them | `accept()` a new connection |
| The waiter | takes the order | reads the request |
| The kitchen | cooks the food | *does the work* (reads a file, runs code) |
| The waiter | brings the food back | sends the response |

A request and a response look like this:

```
Request (what the browser sends)
─────────────────────────────────────────────────────────────
GET /index.html HTTP/1.1          ← tell the server WHAT you want
Host: www.example.com             ← extra information (headers)
                                  ← blank line

Response (what the server sends   back)
─────────────────────────────────────────────────────────────
HTTP/1.1 200 OK                   ← "everything went fine"
Content-Type: text/html           ← what kind of content this is
Content-Length: 1234              ← how many bytes follow
                                  ← blank line
<html> ... the actual web page ... </html>
```

That's it. A web server reads lines like these, decides what they mean,
and writes back an answer.

---

## 🏗️ How Nexus Works, Step by Step

Here is Nexus's simple, never-ending routine:

```
       1. LISTEN ─────► 2. ACCEPT ─────► 3. READ ─────► 4. UNDERSTAND
          └── keep going forever             ▲                          │
                                              │                          ▼
       7. Repeat ◄── 6. SEND RESPONSE ◄── 5. DO THE WORK ◄──────────────┘
```

1. **Listen** — Nexus opens a door (a "port", usually `8080`) and waits.
2. **Accept** — when a browser knocks, Nexus lets it in.
3. **Read** — Nexus receives the request text.
4. **Understand** — Nexus parses the text into the parts it cares about:
   the *method* (`GET`, `POST`, `HEAD`), the *path* (`/api/status`), and
   any *headers* or *body*.
5. **Do the work** — Nexus either:
   - finds a matching **route** (a little piece of C++ code), or
   - serves a **static file** from the `public/` folder.
6. **Send the response** — Nexus writes the answer back to the browser.
7. **Repeat** — back to step 1, ready for the next visitor.

### How Nexus handles many visitors at once

A simple beginner server handles one visitor at a time. That is slow.

Nexus uses a **two-part team**, and the division of labor keeps things fast:

```
              ┌───────────────────────────────┐
              │   THE LISTENER (one person)   │
              │                               │
              │ Watches ALL the doors at once,│
              │ reads incoming requests, and  │
              │ hands each one to a worker.   │
              └───────────────┬───────────────┘
                              │
                    hands off the work
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
   ┌─────────┐          ┌──────────┐          ┌─────────┐
   │ Worker 1│          │ Worker 2 │   ...    │ Worker 4│
   └─────────┘          └──────────┘          └─────────┘
   (reads files,        (reads files,          (reads files,
    runs routes)         runs routes)           runs routes)
```

- The **listener** is the one that pays attention to everyone at the same time.
- The **workers** (4 by default) are the ones that actually do the work.
- When a worker finishes, it sends the answer back to the listener,
  which delivers it to the waiting browser.

This is why one server can talk to many browsers at once without getting
confused — and it's the same basic idea professional servers use.

---

## ✨ Features

| Feature | Plain-English explanation |
| --- | --- |
| **Serves files** | Any file in the `public/` folder can be requested (see the built-in dashboard). |
| **Simple JSON APIs** | A few built-in routes that return data in JSON format. |
| **Multiple visitors at once** | 1 listener + 4 workers, so slow requests don't block fast ones. |
| **Keep-alive** | After answering, the connection stays open so the same browser can make its *next* request without reconnecting. |
| **HEAD support** | A `HEAD` request gets everything a `GET` gets except the file itself — just the "label" (headers). |
| **Safe by default** | Requests that try to escape the `public/` folder get a `403`, and oversized uploads get a `413`. |
| **Graceful shutdown** | Press `Ctrl+C` and the server stops cleanly. |

---

## 🛠️ Build

You need:

- **Linux** (Nexus uses Linux-specific networking tools)
- A C++20 compiler — **GCC 11+** or **Clang 14+**
- `make` (or CMake 3.16+)

Then build with either:

```bash
# Option 1: Make
make

# Option 2: CMake
cmake -B build -S .
cmake --build build -j
```

Both commands produce a single program called `nexus-server` in this
project's folder.

---

## ▶️ Run

```bash
./nexus-server [port]
```

- If you don't type a port, the default is **8080**.
- To use port 5000 instead: `./nexus-server 5000`

Then open your browser and visit <http://localhost:8080>.

You should see the **Nexus dashboard** — a small web page with live status
info, an API tester, and a button that runs a mini stress test against the
server. Try them all.

To stop the server, press **Ctrl+C** in the terminal.

---

## 📡 The Server's Endpoints (API)

The server understands these special addresses:

| Endpoint | Method | What it does |
| --- | --- | --- |
| `/` | `GET` / `HEAD` | The dashboard web page |
| `/api/status` | `GET` | Server health, uptime, worker count |
| `/api/greet?name=<x>` | `GET` | A friendly JSON greeting (uses your `name`) |
| `/api/echo` | `POST` | Sends back whatever body you gave it |

> Any other address tries to load a matching file from `public/`.
> If no such file exists, you get a `404`.

---

## 🎯 Try It Yourself!

Here are three easy queries you can copy and paste. If **curl** is not
installed, you can open these in your browser instead (except the POST one).

**1. Health check (open in your browser, or use curl):**

```bash
curl localhost:8080/api/status
```

You get back something like:

```json
{
  "status": "healthy",
  "uptime_seconds": 12.4,
  "thread_pool_workers": 4,
  "version": "1.0.0"
}
```

**2. A greeting (see how the server handles a `?name=` parameter):**

```bash
curl 'localhost:8080/api/greet?name=Alex'
```

You get back:

```json
{
  "message": "Hello, Alex! Welcome to the C++ Web Server.",
  "query_param_received": "Alex"
}
```

**3. An echo (send data, get it back):**

```bash
curl -X POST -d '{"key":"value"}' localhost:8080/api/echo
```

You get back:

```json
{"key":"value"}
```

---

## 📁 Project Layout

The whole project is small on purpose — you can read every file:

```
├── include/                  # "header" files: they describe the pieces
│   ├── HttpServer.hpp        #   the server itself: listening, routing
│   ├── Connection.hpp        #   info about one connected browser
│   ├── HttpParser.hpp        #   turns raw request text into usable data
│   ├── HttpRequest.hpp       #   one request (method, path, headers, body)
│   ├── HttpResponse.hpp      #   one response (status, headers, body)
│   ├── MimeTypes.hpp         #   file extension → content type (e.g. .png → image/png)
│   └── ThreadPool.hpp        #   the 4 workers that do the real work
├── src/
│   ├── HttpServer.cpp        #   the main engine: accept → read → answer → respond
│   └── main.cpp              #   the entry point that wires everything together
├── public/                   # the static files the server shares (the dashboard)
├── CMakeLists.txt            # build instructions for CMake
├── Dockerfile                # optional: package the server in a "container"
├── Makefile                  # build instructions for make
└── nexus-server              # the compiled program (you built it!)
```

A good way to learn: change something small and re-run `make`.
For example, edit the text in `src/main.cpp` under the `/api/greet`
route and see your dashboard response change.

---

## ⏭️ What Nexus Does Not Do (Yet)

This is a learning project, so some things are left for later:

- **No timeouts** — a browser that connects but stays silent will keep a
  slot busy. Fine for learning, annoying for a real site.
- **No HTTPS** — traffic is plain HTTP. HTTPS is usually handled by another
  program sitting in front of the server.
- **No HTTP/2** — just the classic HTTP/1.1.
- **No configuration file** — settings (port, workers, static folder) live
  in `src/main.cpp`.

---

## 🌐 A Note on Putting This Online

Nexus is great for learning and for running on your own machine or a
private network. If you ever want to expose it to the whole internet,
it's a good idea to put a mature web server (like **Nginx** or **Caddy**)
in front of it to handle HTTPS and security — because real-world web
servers have years of hardening built in, and this project is intentionally
small.

---

*Nexus HTTP Server — C++20, zero dependencies, made for learning.*