# 🔒 HTTPS Server — Concepts, Structures & How It Works

> This document explains how HTTPS works, the structures and concepts behind it, and how it connects to the Nexus C++ HTTP Server project.

---

## 📌 What Is HTTPS?

**HTTPS = HTTP + TLS (Transport Layer Security)**

Plain HTTP sends everything — headers, body, passwords, cookies — as readable text over the network. Anyone sitting between you and the server can read it. HTTPS encrypts all of that so only you and the server can understand it.

```
  HTTP (plain text)          HTTPS (encrypted)
 ─────────────────        ──────────────────
  Client ────"Hello"───►  Server       Anyone on the network sees:
  Server ────"World"───►  Client       gibberish: U%$#@f92kLp!q2
```

HTTPS solves three problems:

1. **Encryption** — Data is encrypted in transit
2. **Authentication** — You know you're talking to the real server (via certificates)
3. **Integrity** — Data can't be tampered with on the way

---

## 🧱 The Core Concept: TLS Handshake

Before any HTTP data is sent, the client and server perform a **TLS handshake**. This is where they agree on encryption keys and verify identity.

### Step-by-step TLS Handshake

```
  Client                                          Server
   │                                                    │
   │──── ClientHello ────────────────────────────────►│
   │     "I'm client X. I support these ciphers."      │
   │                                                    │
   │◄─── ServerHello ──────────────────────────────│
   │     "Great! I'll use TLS 1.3 and AES-256."        │
   │                                                    │
   │◄─── Certificate ─────────────────────────────│
   │     "Here's my identity proof (signed by a        │
   │      trusted authority)."                          │
   │                                                    │
   │◄─── ServerKeyExchange ───────────────────────│
   │     "Here's my public key for this session."      │
   │                                                    │
   │──── ClientKeyExchange ──────────────────────►│
   │     "Here's my part of the shared secret."        │
   │                                                    │
   │──── ChangeCipherSpec ────────────────────────►│
   │     "From now on, everything is encrypted."        │
   │                                                    │
   │──── Finished (encrypted) ────────────────────►│
   │                                                    │
   │◄─── ChangeCipherSpec ───────────────────────│
   │◄─── Finished (encrypted) ──────────────────│
   │                                                    │
   │──── HTTP Request (encrypted) ────────────────►│
   │──── HTTP Response (encrypted) ──────────────│
```

**In plain English:**
1. The client says "hello" and lists what encryption methods it supports
2. The server picks one and proves its identity with a certificate
3. Both sides derive a shared secret key (without ever sending it directly)
4. From that point on, all communication is encrypted

---

## 📦 Key Structures Needed for HTTPS

### 1. Certificate

A certificate is like a digital ID card. It contains:

```
┌─────────────────────────────────────┐
│         DIGITAL CERTIFICATE          │
├─────────────────────────────────────┤
│  Subject: example.com               │  ← Who this cert belongs to
│  Issuer: Let's Encrypt              │  ← Who verified it
│  Public Key: -----BEGIN            │  ← The server's public key
│    PUBLIC KEY -----                 │
│  Valid From: Jan 1, 2026            │  ← When it starts working
│  Valid Until: Jan 1, 2027           │  ← When it expires
│  Signature Algorithm: RSA-SHA256    │  ← How it was signed
│  Serial Number: a1b2c3d4           │  ← Unique ID
└─────────────────────────────────────┘
```

**Why it matters:** When your browser connects to an HTTPS server, the server presents this certificate. The browser checks if a trusted authority signed it and if it's still valid. If not, it shows a scary warning.

### 2. Private Key

The private key is the secret half of a key pair. It's stored on the server and **never shared**.

```
  Public Key  ──► encrypts data     ◄── Private Key (kept secret)
                    ↓                         ↑
              Anyone can encrypt    Only the server can decrypt
```

**Files on the server:**
```
server.key        ← Private key (KEEP THIS SECRET, permissions 600)
server.crt        ← Certificate (public, can be shared)
ca-bundle.crt     ← Certificate chain (intermediate certs)
```

### 3. SSL/TLS Context

This is the main structure that holds all TLS configuration. Think of it as the "TLS engine" for your server.

```cpp
// Conceptual structure for an SSL/TLS context
struct TLSContext {
    std::string certPath;      // Path to server.crt
    std::string keyPath;       // Path to server.key
    std::string caPath;        // Path to ca-bundle.crt
    int protocolVersion;       // TLS 1.2, TLS 1.3
    std::vector<std::string> cipherSuites;  // Allowed encryption methods
    bool verifyClient;         // Whether to require client certificates
};
```

### 4. Encrypted Connection

Once the TLS handshake is done, the connection becomes an encrypted tunnel. All HTTP data flows through it.

```
┌─────────────────────────────────────────┐
│            ENCRYPTED TUNNEL              │
│                                          │
│  ┌───────────┐  ┌───────────┐          │
│  │ TLS Layer │  │ TLS Layer │          │
│  │ (encrypt) │→→│ (decrypt) │          │
│  └─────┬─────┘  └─────┬─────┘          │
│        │              │                │
│  ┌─────▼─────┐  ┌─────▼─────┐          │
│  │  HTTP     │  │  HTTP     │          │
│  │  Request  │  │ Response  │          │
│  │  (plain)  │  │ (plain)   │          │
│  └───────────┘  └───────────┘          │
│                                          │
│  The HTTP layer doesn't change at all!   │
│  TLS just wraps it transparently.        │
└─────────────────────────────────────────┘
```

---

## 🔄 How HTTPS Works With an HTTP Server

### The Architecture Change

Adding HTTPS to our Nexus server is simpler than it seems. The HTTP logic stays **exactly the same**. TLS just wraps the socket.

```
  WITHOUT HTTPS:
  ──────────────
  Client ←────→ [Socket] ←────→ [epoll] ←────→ [HTTP Parser] ←────→ [Route Handler]

  WITH HTTPS:
  ────────────
  Client ←────→ [TLS Layer] ←────→ [Socket] ←────→ [epoll] ←────→ [HTTP Parser] ←────→ [Route Handler]
                    ↑                        ↑
               [TLS Context]          [SSL_accept / SSL_connect]
```

### The Socket Upgrade Process

```
  Step 1: Create a regular TCP socket (same as before)
          ↓
  Step 2: Create a TLS context (load certs, keys, settings)
          ↓
  Step 3: Wrap the socket in TLS
          ↓
  Step 4: Client connects → perform TLS handshake
          ↓
  Step 5: Handshake complete → now read/write encrypted HTTP
          ↓
  Step 6: Everything downstream is IDENTICAL to plain HTTP
          (parse, route, respond)
```

```cpp
// Conceptual code showing the minimal change needed

// BEFORE (plain HTTP):
listenSocket = socket(AF_INET, SOCK_STREAM, 0);
bind(listenSocket, ...);
listen(listenSocket, ...);
clientSocket = accept(listenSocket, ...);
// Now read/write directly with recv()/send()

// AFTER (HTTPS):
listenSocket = socket(AF_INET, SOCK_STREAM, 0);
bind(listenSocket, ...);
listen(listenSocket, ...);

// NEW: Create TLS context
SSL_CTX* tlsCtx = SSL_CTX_new(TLS_server_method());
SSL_CTX_use_certificate_file(tlsCtx, "server.crt", SSL_FILETYPE_PEM);
SSL_CTX_use_PrivateKey_file(tlsCtx, "server.key", SSL_FILETYPE_PEM);

// NEW: Accept and wrap in TLS
int clientSocket = accept(listenSocket, ...);
SSL* ssl = SSL_new(tlsCtx);       // Create TLS object
SSL_set_fd(ssl, clientSocket);    // Attach to socket
SSL_accept(ssl);                  // Perform TLS handshake ← THE KEY STEP

// Everything after this is IDENTICAL:
// SSL_read(ssl, ...) instead of recv()
// SSL_write(ssl, ...) instead of send()
// The HTTP parser and route handlers don't change at all!
```

---

## 📊 HTTPS vs HTTP — Side by Side

| Aspect | HTTP | HTTPS |
| --- | --- | --- |
| **Port** | 80 | 443 |
| **Protocol** | TCP directly | TCP → TLS → HTTP |
| **Data** | Plain text | Encrypted |
| **Certificate** | Not needed | Required |
| **Speed** | Slightly faster | Slightly slower (handshake overhead) |
| **Security** | None | Encrypted + authenticated |
| **URL** | `http://` | `https://` |
| **Lock icon in browser** | ❌ No | ✅ Yes |

---

## 🔑 Certificate Types Explained Simply

### Self-Signed Certificate

```
  You make your own ID card and say "I'm the boss, trust me"

  ✅ Easy to set up
  ✅ Free
  ❌ Browser shows "Not Secure" warning
  ❌ Users see a scary alert

  Use for: Testing, local development
```

### Certificate from a Certificate Authority (CA)

```
  A trusted third party (Let's Encrypt, DigiCert, etc.)
  verifies your identity and signs your certificate

  ✅ Browser trusts it automatically
  ✅ No scary warnings
  ✅ Users feel safe
  ❌ Costs money (sometimes free, like Let's Encrypt)
  ❌ Requires domain ownership verification

  Use for: Production servers
```

### Certificate Chain

```
  Root CA (trusted by browsers)
      │
      │ signs
      ▼
  Intermediate CA (the middleman)
      │
      │ signs
      ▼
  Your Server Certificate

  The chain proves: "Let's Encrypt trusts this cert,
  and browsers trust Let's Encrypt."
```

---

## 🧩 How HTTPS Integrates With Nexus

### The TLS Layer Sits Between the Socket and the HTTP Parser

```
┌─────────────────────────────────────────────────┐
│                  NEXUS SERVER                    │
│                                                    │
│  ┌─────────────────────────────────────────────┐ │
│  │              HTTP APPLICATION LAYER          │ │
│  │  Route handlers, static files, JSON responses │ │
│  │  (This code DOESN'T CHANGE)                  │ │
│  └──────────────────────┬──────────────────────┘ │
│                         │                        │
│  ┌──────────────────────▼──────────────────────┘ │
│  │              TLS / SSL LAYER                   │ │
│  │  SSL_read()  ← reads decrypted data          │ │
│  │  SSL_write() ← sends encrypted data          │ │
│  │  SSL_accept() ← TLS handshake (server side)  │ │
│  │  SSL_CTX_new() ← set up TLS configuration    │ │
│  │  Certificate loading                          │ │
│  └──────────────────────┬──────────────────────┘ │
│                         │                        │
│  ┌──────────────────────▼──────────────────────┘ │
│  │              SOCKET LAYER                      │ │
│  │  accept(), recv(), send()                      │ │
│  │  epoll, non-blocking I/O                       │ │
│  └──────────────────────┬──────────────────────┘ │
│                         │                        │
│                         ▼                        │
│                    NETWORK                        │
└─────────────────────────────────────────────────┘
```

**The key insight:** The HTTP parser and route handlers never change. TLS just changes how data moves in and out of the socket.

---

## 📋 What Changes in the Code

### Files That Stay The Same

| File | Why It Doesn't Change |
| --- | --- |
| `HttpParser.hpp` | Parses HTTP text — encrypted or not, the text looks the same |
| `HttpRequest.hpp` | Request model — same structure |
| `HttpResponse.hpp` | Response model — same structure |
| `Connection.hpp` | Connection state — same flags and buffers |
| `ThreadPool.hpp` | Worker threads — same task queue |
| `MimeTypes.hpp` | File type detection — same mapping |
| `HttpServer.cpp` | Event loop logic — same flow, just use `SSL_read`/`SSL_write` |
| `main.cpp` | Route handlers — same logic |

### Files That Change or Are Added

| File | What Changes |
| --- | --- |
| `include/HttpServer.hpp` | Add `#include <openssl/ssl.h>`, add `SSL_CTX*` member |
| `src/HttpServer.cpp` | Add TLS context setup, `SSL_accept` in connection handling |
| `include/Connection.hpp` | Add `SSL* ssl` field to store the TLS object |
| `src/main.cpp` | Load certificates, start TLS on specified port |
| `Dockerfile` | Install OpenSSL libraries |
| `Dockerfile` | Copy certificate files into the image |

---

## 🔄 The HTTPS Connection Lifecycle

```
  Client                                          Nexus Server
   │                                                    │
   │──── TCP SYN ────────────────────────────────►│
   │◄─── TCP SYN-ACK ─────────────────────────│
   │──── TCP ACK ──────────────────────────────►│
   │                                                    │
   │──── ClientHello (TLS) ────────────────────►│
   │◄─── ServerHello + Certificate ──────────│
   │◄─── ServerKeyExchange ──────────────────│
   │──── ClientKeyExchange ──────────────────►│
   │──── ChangeCipherSpec ──────────────────►│
   │──── Finished (encrypted) ──────────────►│
   │◄─── ChangeCipherSpec ─────────────────│
   │◄─── Finished (encrypted) ────────────│
   │                                                    │
   │  ←── TLS HANDSHAKE COMPLETE ──→                   │
   │                                                    │
   │──── HTTPS GET /api/status ──────────────►│
   │     (data is encrypted)                    │
   │                                                    │
   │  ←── TLS layer decrypts → HTTP parser gets plain ──│
   │  ←── Route handler runs → response built         │
   │  ←── TLS layer encrypts → send()                 │
   │◄─── HTTPS 200 OK ──────────────────────│
   │     (data is encrypted)                    │
```

---

## 📐 Certificate File Structure for the Project

```
HTTP-Server-in-C/
├── certs/                      ← NEW directory for TLS certificates
│   ├── server.crt              ← Server certificate (public)
│   ├── server.key              ← Private key (KEEP SECRET!)
│   ├── ca-bundle.crt           ← Certificate authority chain
│   └── dhparams.pem            ← Diffie-Hellman parameters (for perfect forward secrecy)
├── include/
│   ├── HttpServer.hpp          ← Add SSL_CTX*, SSL* fields
│   └── Connection.hpp          ← Add SSL* ssl field
├── src/
│   ├── HttpServer.cpp          ← Add TLS setup and handshake code
│   └── main.cpp                ← Load certs, start TLS on port 443
├── Dockerfile                  ← Install libssl-dev, copy certs
└── ...
```

### Generating a Self-Signed Certificate for Testing

```bash
# Create certs directory
mkdir -p certs

# Generate a private key
openssl genrsa -out certs/server.key 2048

# Generate a self-signed certificate (valid for 365 days)
openssl req -new -x509 -key certs/server.key -out certs/server.crt -days 365

# Set secure permissions on the key
chmod 600 certs/server.key
```

---

## 🔧 How to Add HTTPS Support — Step by Step

### Step 1: Install OpenSSL

**Makefile:**
```makefile
CXXFLAGS += -I/usr/include/openssl
LDLIBS += -lssl -lcrypto
```

**CMakeLists.txt:**
```cmake
find_package(OpenSSL REQUIRED)
target_link_libraries(nexus-server PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)
```

**Dockerfile:**
```dockerfile
RUN apt-get update && apt-get install -y libssl-dev ca-certificates
```

### Step 2: Include OpenSSL Headers

```cpp
#include <openssl/ssl.h>
#include <openssl/err.h>
```

### Step 3: Initialize TLS in the Constructor

```cpp
HttpServer::HttpServer(const std::string& address, int port, size_t threadPoolSize)
    : ipAddress(address), serverPort(port), threadPool(threadPoolSize) {
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create TLS context
    tlsCtx = SSL_CTX_new(TLS_server_method());
    if (!tlsCtx) {
        throw std::runtime_error("Failed to create SSL context");
    }
    
    // Load certificates
    if (SSL_CTX_use_certificate_file(tlsCtx, "certs/server.crt", SSL_FILETYPE_PEM) <= 0) {
        throw std::runtime_error("Failed to load certificate");
    }
    if (SSL_CTX_use_PrivateKey_file(tlsCtx, "certs/server.key", SSL_FILETYPE_PEM) <= 0) {
        throw std::runtime_error("Failed to load private key");
    }
    
    // Verify the private key matches the certificate
    if (!SSL_CTX_check_private_key(tlsCtx)) {
        throw std::runtime_error("Private key does not match certificate");
    }
}
```

### Step 4: Wrap Accepted Connections in TLS

```cpp
void HttpServer::acceptConnections() {
    while (true) {
        int client = accept4(listenSocket, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            continue;
        }
        
        // NEW: Wrap in TLS
        SSL* ssl = SSL_new(tlsCtx);
        SSL_set_fd(ssl, client);
        
        int ret = SSL_accept(ssl);
        if (ret <= 0) {
            // TLS handshake failed
            SSL_free(ssl);
            close(client);
            continue;
        }
        
        // Store the SSL object in the Connection struct
        connections[client] = std::make_unique<Connection>(client);
        connections[client]->ssl = ssl;  // Store for later use
        
        // Register with epoll (same as before)
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = client;
        epoll_ctl(epollFd, EPOLL_CTL_ADD, client, &ev);
    }
}
```

### Step 5: Replace recv()/send() With SSL_read()/SSL_write()

```cpp
// In readConnection():
// BEFORE:
// ssize_t n = recv(conn.fd, buffer, sizeof(buffer), 0);

// AFTER:
ssize_t n = SSL_read(conn.ssl, buffer, sizeof(buffer));

// In writeConnection():
// BEFORE:
// ssize_t n = send(conn.fd, ..., MSG_NOSIGNAL);

// AFTER:
ssize_t n = SSL_write(conn.ssl, conn.outBuf.data() + conn.outOffset, ...);
```

### Step 6: Clean Up on Shutdown

```cpp
// Close TLS connection
SSL_shutdown(conn.ssl);
SSL_free(conn.ssl);

// Cleanup TLS context
SSL_CTX_free(tlsCtx);
EVP_cleanup();
```

---

## 🔐 Encryption Algorithms Explained Simply

### Symmetric Encryption (for data in transit)

After the handshake, both sides use the same key to encrypt/decrypt. Fast and efficient.

```
  Key: "secret123"
  Plaintext: "Hello"
  Encrypted: "X§9f$k"
  Decrypted: "Hello"  ← using the same key
```

**Common algorithms:** AES-256-GCM (most common), ChaCha20-Poly1305

### Asymmetric Encryption (for the handshake)

Two different keys — public key to encrypt, private key to decrypt. Used only during the TLS handshake to establish the shared secret.

```
  Public Key: "lock-open"  → anyone can lock
  Private Key: "lock-close" → only the server can unlock
```

**Common algorithms:** RSA (2048-bit or 4096-bit), ECDSA

### Key Exchange (how both sides get the same key)

```
  Client generates a random number → sends to server encrypted with server's public key
  Server generates a random number → sends to client encrypted with server's public key
  Both combine their random numbers → arrive at the same shared secret
  Shared secret → used as the symmetric encryption key
```

---

## 📈 HTTPS Performance Considerations

### The TLS Handshake Cost

```
  TCP Handshake:     1 round trip (SYN, SYN-ACK, ACK)
  TLS 1.2 Handshake: 2 round trips
  TLS 1.3 Handshake: 1 round trip (faster!)
  ─────────────────────────────────────
  Total (HTTPS):     2-3 round trips before any HTTP data
```

### Session Resumption

To avoid repeating the expensive handshake for every connection:

```
  First connection:   Full TLS handshake (2-3 round trips)
  Second connection:  Resumed session (0-1 round trips)

  How: The server issues a "session ticket" after the first handshake.
       The client presents it next time, and they skip straight to encryption.
```

### Session Tickets

```
  Server ──→ Client: "Here's your session ticket (encrypted)"
  Client stores it
  Client ──→ Server: "Hi again! Here's my ticket from last time"
  Server: "Valid! Skip handshake, go straight to encrypted data"
```

---

## 🏗️ Project Extension Map

Here's how HTTPS would extend the current Nexus project:

```
  CURRENT PROJECT (HTTP)          EXTENDED PROJECT (HTTPS)
  ──────────────────────          ─────────────────────────
  Socket → recv() → parse()      Socket → TLS → decrypt() → parse()
                    ↓                                  ↓
              Route handler                  Route handler (SAME!)
                    ↓                                  ↓
              serialize → send()                 serialize → encrypt() → send()

  Changes needed:
  ──────────────
  1. Add OpenSSL dependency (3 lines of config)
  2. Add SSL_CTX to server (constructor/destructor)
  3. Add SSL* to Connection struct
  4. Wrap accept() with SSL_accept()
  5. Swap recv()/send() for SSL_read()/SSL_write()
  6. All HTTP logic: UNCHANGED
```

---

## 🧪 Testing HTTPS

### Test with curl:

```bash
# Using self-signed certificate (ignore warning)
curl -k https://localhost:8443/api/status

# Using the certificate file
curl --cacert certs/server.crt https://localhost:8443/api/status

# Verbose output to see the TLS handshake details
curl -vk --cacert certs/server.crt https://localhost:8443/api/status
```

### Test with openssl directly:

```bash
# Connect and inspect the certificate
openssl s_client -connect localhost:8443 -servername localhost

# Test cipher suites
openssl ciphers -v 'TLS1.3'
```

### Expected output:

```
New, TLSv1.3, Cipher is TLS_AES_256_GCM_SHA384
Server public key is 2048 bit
Secure Renegotiation IS NOT supported
Compression: NONE
Expansion: NONE
No ALPN negotiated
SSL-Session:
    Protocol  : TLSv1.3
    Cipher    : TLS_AES_256_GCM_SHA384
    ...
```

---

## 📊 Complete Architecture Diagram (HTTP + HTTPS)

```
┌──────────────────────────────────────────────────────────────────┐
│                        NETWORK LAYER                             │
│                     (TCP/IP stack)                               │
└──────────────────────────┬───────────────────────────────────────┘
                           │
          ┌────────────────┼────────────────┐
          │                │                │
          ▼                ▼                ▼
    Port 80 (HTTP)   Port 443 (HTTPS)   Port 8080 (custom)
          │                │                │
          ▼                ▼                ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│  TLS Layer   │  │  TLS Layer   │  │   NONE       │
│  (Decrypt)   │  │  (Decrypt)   │  │   (plain)    │
└──────┬───────┘  └──────┬───────┘  └──────┬───────┘
       │                 │                 │
       ▼                 ▼                 ▼
┌─────────────────────────────────────────────────────┐
│              NEXUS HTTP APPLICATION LAYER             │
│                                                       │
│  ┌───────────────────────────────────────────────┐   │
│  │         Epoll Event Loop (1 thread)           │   │
│  │  ┌─────────┐  ┌─────────┐  ┌──────────────┐  │   │
│  │  │ accept  │  │ recv    │  │  send        │  │   │
│  │  │ accept4 │→→│ SSL_read│→→│ SSL_write   │  │   │
│  │  └─────────┘  └────┬────┘  └──────────────┘  │   │
│  │                     │                          │   │
│  │              ┌──────▼──────┐                   │   │
│  │              │ HttpParser  │                   │   │
│  │              │ ::extract() │                   │   │
│  │              └──────┬──────┘                   │   │
│  │                     │                          │   │
│  │              ┌──────▼──────┐                   │   │
│  │              │ dispatch()  │                   │   │
│  │              └──────┬──────┘                   │   │
│  │                     │                          │   │
│  │              ┌──────▼──────┐                   │   │
│  │              │ ThreadPool  │                   │   │
│  │              │ (4 workers) │                   │   │
│  │              └──────┬──────┘                   │   │
│  │                     │                          │   │
│  │              ┌──────▼──────┐                   │   │
│  │              │ Route Match │                   │   │
│  │              │ (exact)     │                   │   │
│  │              └──────┬──────┘                   │   │
│  │                     │                          │   │
│  │              ┌──────▼──────┐                   │   │
│  │              │ Handler     │                   │   │
│  │              │ Execution   │                   │   │
│  │              └──────┬──────┘                   │   │
│  │                     │                          │   │
│  │              ┌──────▼──────┐                   │   │
│  │              │ Static File │                   │   │
│  │              │ Serving     │                   │   │
│  │              └─────────────┘                   │   │
│  └───────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

---

## 🔑 Security Best Practices

### Certificate Management

```
✅ DO:
  - Use certificates from a trusted CA (Let's Encrypt is free)
  - Set up automatic renewal (certbot)
  - Use TLS 1.2 or TLS 1.3 minimum
  - Enable HSTS (HTTP Strict Transport Security)
  - Use strong cipher suites only

❌ DON'T:
  - Use self-signed certificates in production
  - Use TLS 1.0 or 1.1 (deprecated, insecure)
  - Allow weak ciphers (RC4, DES, MD5)
  - Hardcode certificates in source code
  - Expose private keys in version control
```

### Recommended TLS Configuration

```cpp
// In the TLS context setup
SSL_CTX_set_min_proto_version(tlsCtx, TLS1_2_VERSION);  // No TLS 1.0/1.1
SSL_CTX_set_ciphersuites(tlsCtx, 
    "TLS_AES_256_GCM_SHA384:"
    "TLS_CHACHA20_POLY1305_SHA256:"
    "TLS_AES_128_GCM_SHA256");
```

### Perfect Forward Secrecy (PFS)

```
  Without PFS:
    If someone records encrypted traffic today and steals the
    server's private key later, they can decrypt everything.

  With PFS:
    Each session uses a unique key. Even if the server's private
    key is stolen, past sessions remain encrypted.

  How: Ephemeral Diffie-Hellman key exchange (DHE or ECDHE)
```

---

## 📝 Summary — What You Need to Know

| Concept | Simple Explanation |
| --- | --- |
| **HTTPS** | HTTP but encrypted with TLS |
| **TLS Handshake** | Client and server agree on encryption keys before sending data |
| **Certificate** | Digital ID card that proves the server is who it claims to be |
| **Private Key** | Secret key stored only on the server |
| **SSL/TLS Context** | Configuration object holding certificates, protocols, and settings |
| **Session Resumption** | Skip the expensive handshake for repeat visitors |
| **Perfect Forward Secrecy** | Each session has unique keys; stealing the server key doesn't decrypt past traffic |
| **Integration with Nexus** | Only the socket layer changes (recv→SSL_read, send→SSL_write). Everything else stays identical |

---

*This document covers the complete conceptual foundation for adding HTTPS to the Nexus HTTP Server. The core principle is simple: TLS wraps the socket, HTTP logic stays the same.*

*For implementation, refer to the OpenSSL documentation and the `certs/` directory that would be added to the project.*
