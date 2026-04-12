# Architecture Overview

Aevox is built as a strict layered system. Each layer depends only on the layer directly below it. No layer skips across boundaries.

```
[ Your Application Code ]
        ↓
[ aevox::Router + Middleware ]     include/aevox/
        ↓
[ aevox::Executor ]                include/aevox/executor.hpp  ← public boundary
        ↓  (no Asio types cross upward)
[ Asio implementation ]            src/net/
        ↓
[ OS I/O: io_uring / kqueue / IOCP ]
        ↓
[ Reverse Proxy: Nginx / Caddy ]   HTTP/2, HTTP/3, TLS live here
```

## Design Principles

- **`aevox::Executor` is the firewall.** No Asio type appears above it — ever. This is the migration path to `std::net` (C++29).
- **Public headers are dependency-free.** Only standard library headers may be included from `include/aevox/`.
- **Errors are values.** `std::expected<T, E>` throughout — no exceptions for control flow.
- **Async is coroutines.** `aevox::Task<T>` is the only async primitive exposed to users.

## Architecture Design Documents

ADDs live in `Tasks/architecture/`. Each ADD covers one task's design in full detail.

| ADD | Task | Status |
|---|---|---|
| [AEV-001-arch.md](../../Tasks/architecture/AEV-001-arch.md) | Asio-based async I/O core | Draft |
