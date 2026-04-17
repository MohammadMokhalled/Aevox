# Architecture Overview

Aevox is built as a strict layered system. Each layer depends only on the layer directly below it — no layer skips across boundaries, and no internal type leaks upward into the public API.

---

## Layer Diagram

```mermaid
graph TD
    subgraph Public["Public API — <code>include/aevox/</code>"]
        APP["<code>aevox::App</code><br/><em>planned</em>"]
        ROUTER["<code>aevox::Router</code><br/><em>planned</em>"]
        RR["<code>aevox::Request / Response</code><br/><em>planned</em>"]
        EXEC["<code>aevox::Executor</code><br/><code>aevox::TcpStream</code><br/><strong>implemented</strong>"]
        TASK["<code>aevox::Task&lt;T&gt;</code><br/><code>aevox::pool / sleep / when_all</code><br/><strong>implemented</strong>"]
    end

    subgraph Internal["Internal — <code>src/</code>"]
        HTTP_PARSER["HTTP/1.1 Parser<br/><code>src/http/</code> — llhttp hidden<br/><strong>implemented</strong>"]
        NET["Async TCP / Executor impl<br/><code>src/net/</code> — Asio hidden<br/><strong>implemented</strong>"]
    end

    subgraph OS["OS / Runtime"]
        ASIO["Asio <code>io_context</code><br/>I/O + CPU thread pools"]
        OSIO["OS I/O<br/>io_uring · kqueue · IOCP"]
        PROXY["Reverse Proxy<br/>Nginx / Caddy<br/>TLS · HTTP/2 · HTTP/3"]
    end

    APP --> ROUTER --> RR --> EXEC
    EXEC --> HTTP_PARSER
    EXEC --> NET
    NET --> ASIO --> OSIO --> PROXY
```

---

## Key Design Invariants

### 1. Executor Firewall (ADR-1)

`aevox::Executor` in `include/aevox/executor.hpp` is the **only networking abstraction** visible to application code. No Asio type (`asio::io_context`, `asio::awaitable`, `asio::thread_pool`) ever appears above this boundary.

```mermaid
graph LR
    PUB["include/aevox/ — Asio-free zone"]
    SRC["src/net/ — Asio lives here only"]
    PUB -->|"Executor interface<br/>(no Asio types)"| SRC
    SRC -.->|"BANNED: any Asio type"| PUB
```

**Why:** When `std::net` standardises in C++29, replacing Asio requires changes only in `src/net/`. Zero application-code changes.

### 2. No Third-Party Types in Public Headers (ADR-1)

`include/aevox/` may only include C++ standard library headers. llhttp, glaze, spdlog, fmtlib, and Asio are all implementation details of `src/`.

### 3. Errors as Values (PRD §6.4)

`std::expected<T, E>` is the error model everywhere. Exceptions are never used for control flow — they only propagate from third-party code through coroutine machinery.

### 4. Async as Coroutines (ADR-3)

`aevox::Task<T>` is the only async primitive exposed publicly. No callbacks appear in the public API. The handler signature `Task<void>(uint64_t, TcpStream)` is enforced by the `ConnectionHandler` concept.

---

## Thread Model

```mermaid
flowchart LR
    subgraph IO["I/O Thread Pool (thread_count = hwconc by default)"]
        T1["I/O Thread 1\nAcceptLoop · Coroutine dispatch"]
        T2["I/O Thread 2\nAcceptLoop · Coroutine dispatch"]
        TN["I/O Thread N\n..."]
    end

    subgraph CPU["CPU Thread Pool (cpu_pool_threads = 4 by default)"]
        C1["CPU Thread 1\naevox::pool() tasks"]
        C2["CPU Thread 2\naevox::pool() tasks"]
        C3["CPU Thread 3\naevox::pool() tasks"]
        C4["CPU Thread 4\naevox::pool() tasks"]
    end

    ACCEPT["Asio acceptor"] --> T1
    ACCEPT --> T2

    T1 -->|"pool(fn)"| C1
    T2 -->|"pool(fn)"| C2

    C1 -->|"resume coroutine"| T1
    C2 -->|"resume coroutine"| T2
```

**Rules:**

- All coroutines run on I/O threads by default (ADR-3: pinned to originating thread in v0.1).
- `aevox::pool()` moves CPU-bound work to the CPU pool, suspending the coroutine. When done, the coroutine resumes on an I/O thread.
- `aevox::sleep()` suspends via an Asio timer — no thread is occupied during the wait.
- Thread-local bridges (`tl_post_to_io`, `tl_post_to_cpu`, `tl_schedule_after`) are set on each I/O thread by `AsioExecutor` and used by `pool()`, `sleep()`, and `when_all()`.

---

## Request Flow (Current — Raw TCP)

```mermaid
sequenceDiagram
    participant Client
    participant OS
    participant Executor
    participant Coroutine as "Handler Coroutine"
    participant Parser as "HttpParser (internal)"

    Client->>OS: TCP connect
    OS->>Executor: accept()
    Executor->>Coroutine: spawn(conn_id, TcpStream)

    loop Connection alive
        Coroutine->>Coroutine: co_await stream.read()
        Client->>OS: send HTTP bytes
        OS-->>Coroutine: resume with vector<byte>
        Coroutine->>Parser: parser.feed(span)
        alt Incomplete
            Parser-->>Coroutine: ParseError::Incomplete
        else Complete
            Parser-->>Coroutine: ParsedRequest
            Coroutine->>Coroutine: process request
            Coroutine->>Coroutine: co_await stream.write(response)
            Coroutine-->>OS: bytes sent
        end
    end

    Client->>OS: TCP close (FIN)
    OS-->>Coroutine: IoError::Eof
    Coroutine-->>Executor: co_return (TcpStream destructs → socket closes)
```

---

## Pimpl Pattern

Both `TcpStream` and `HttpParser` (internal) use pimpl (`std::unique_ptr<Impl>`) to hide their concrete types:

```mermaid
classDiagram
    class TcpStream {
        +read() Task~expected~
        +write() Task~expected~
        +valid() bool
        -impl_ unique_ptr~Impl~
    }
    class TcpStreamImpl["TcpStream::Impl (src/net/)"] {
        +socket asio::ip::tcp::socket
        +io_ctx asio::io_context&
    }
    class HttpParser["HttpParser (src/http/)"] {
        +feed() expected~ParsedRequest~
        +reset() void
        -impl_ unique_ptr~Impl~
    }
    class HttpParserImpl["HttpParser::Impl (src/http/)"] {
        +parser_ llhttp_t
        +settings_ llhttp_settings_t
        +pending ParsedRequest
        +chunk_buf vector~byte~
    }

    TcpStream --> TcpStreamImpl : "unique_ptr (pimpl)"
    HttpParser --> HttpParserImpl : "unique_ptr (pimpl)"
```

---

## HTTP Parser Layer

`aevox::detail::HttpParser` in `src/http/` wraps llhttp with an incremental feed model:

```mermaid
flowchart TD
    FEED["feed(span&lt;byte&gt;)"] --> EXEC["llhttp_execute()"]
    EXEC -->|"HPE_PAUSED"| COMPLETE["message complete\nReturn ParsedRequest"]
    EXEC -->|"HPE_OK"| INCOMPLETE["Return ParseError::Incomplete\n(need more data)"]
    EXEC -->|"HPE_USER"| TOOLARGE["Return ParseError::TooLarge\nor TooManyHeaders"]
    EXEC -->|"other"| BADREQ["Return ParseError::BadRequest"]

    subgraph Callbacks["llhttp callbacks (internal)"]
        CB1["on_url → target"]
        CB2["on_method → method"]
        CB3["on_header_field/value → headers[]"]
        CB4["on_body → chunk_buf"]
        CB5["on_message_complete → return HPE_PAUSED"]
    end
    EXEC --> Callbacks
```

**Constraints enforced by `ParserConfig`:**

| Config field | Default | Protection |
|---|---|---|
| `max_header_count` | 100 | Hash-flood / DoS on header map |
| `max_body_bytes` | 1 MiB | Memory exhaustion on large uploads |

**Zero-copy note:** `ParsedRequest::method`, `target`, and `headers` are `string_view` / `span` into the caller's buffer. The caller must keep the buffer alive while using the result.

---

## File Structure

```
aevox/
├── include/aevox/          # PUBLIC API — no Asio, no third-party types
│   ├── executor.hpp        # Executor, ExecutorConfig, ExecutorError, ConnectionHandler
│   ├── task.hpp            # Task<T>, Task<void>
│   ├── async.hpp           # pool(), sleep(), when_all()
│   └── tcp_stream.hpp      # TcpStream, IoError
│
├── src/
│   ├── net/                # ALL Asio code lives here only
│   │   ├── asio_executor.hpp/.cpp      # AsioExecutor implements Executor
│   │   └── asio_tcp_stream.hpp/.cpp    # TcpStream::Impl + ReadAwaitable/WriteAwaitable
│   └── http/               # HTTP parsing — llhttp confined here
│       ├── http_parser.hpp             # HttpParser, ParsedRequest, ParseError (internal)
│       └── http_parser.cpp             # llhttp callbacks and feed() logic
│
└── tests/
    ├── unit/
    │   ├── net/    # AEV-001, AEV-006 unit tests
    │   └── http/   # AEV-003 HTTP parser unit tests
    └── integration/
        ├── net/    # AEV-001, AEV-006 integration tests (real loopback)
        └── http/   # AEV-003 integration tests (real loopback + parser)
```

---

## Architecture Design Documents

ADDs live in `Tasks/architecture/`. Each ADD covers one task's design in full — open issues, file maps, API sketches, and deviation records.

| ADD | Task | Status |
|---|---|---|
| `Tasks/architecture/AEV-001-arch.md` | Asio-backed executor, `Task<T>`, TCP accept loop | Done |
| `Tasks/architecture/AEV-003-arch.md` | `TcpStream`, HTTP/1.1 parser (llhttp), `ConnectionHandler` breaking change | Done |
| `Tasks/architecture/AEV-006-arch.md` | CPU thread pool, `pool()`, `sleep()`, `when_all()` | Done |

---

## ADR Summary

| ADR | Decision |
|---|---|
| ADR-1 | Asio hidden behind `aevox::Executor`. Enables `std::net` swap in C++29 with zero app changes. |
| ADR-2 | `aevox::pool()` uses a separate CPU thread pool — never the I/O pool, to prevent starvation. |
| ADR-3 | Coroutines are pinned to their originating I/O thread in v0.1. Cross-thread migration deferred. |
| ADR-4 | Regex routing is opt-in, not available in v0.1. |
| ADR-5 | C++20 modules are opt-in in v0.4. |
| ADR-6 | HTTP/2 is permanently out of scope — delegated to Nginx/Caddy. |
