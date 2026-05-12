# WebSocket Chat

Minimal WebSocket chat example demonstrating room-based broadcast with Aevox. Multiple clients connect to `ws://localhost:8080/chat/{room}`; any message sent to a room is broadcast to all other clients in that room.

**Source:** [`examples/websocket-chat/main.cpp`](https://github.com/MohammadMokhalled/Aevox/blob/main/examples/websocket-chat/main.cpp)

---

## Build and run

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build --preset default --target websocket-chat
./build/debug/examples/websocket-chat/websocket-chat
```

The server listens on port 8080. Stop it with `Ctrl-C`.

---

## Connect a client

Use any WebSocket client (browser `WebSocket`, `wscat`, etc.):

```bash
# Connect to the "general" room
wscat -c ws://localhost:8080/chat/general

# In another terminal, connect to the same room
wscat -c ws://localhost:8080/chat/general
```

Messages typed in one client are broadcast to all other clients in the same room. The sender does not receive its own message (self-publish suppression).

---

## Full source

```cpp
#include <aevox/app.hpp>
#include <aevox/websocket_handler.hpp>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string_view>

int main()
{
    aevox::App app;

    app.ws("/chat/{room}",
           aevox::WebSocketHandler{
               .on_open =
                   [](aevox::WebSocket& ws) {
                       ws.subscribe(ws.topic());
                       std::cout << std::format("[open]  client joined room '{}'\n", ws.topic());
                   },

               .on_message =
                   [](aevox::WebSocket& ws, std::string_view msg) {
                       ws.publish(ws.topic(), msg);
                   },

               .on_close =
                   [](aevox::WebSocket& ws, std::uint16_t code) {
                       std::cout << std::format("[close] client left room '{}' (code {})\n",
                                                ws.topic(), code);
                   },
           });

    std::cout << "WebSocket chat server listening on ws://localhost:8080/chat/{room}\n";
    app.listen(8080);

    return EXIT_SUCCESS;
}
```

---

## What this covers

- **`app.ws(pattern, handler)`** — registers a WebSocket route with automatic HTTP upgrade
- **`aevox::WebSocketHandler`** — lifecycle callback aggregate (`on_open`, `on_message`, `on_close`)
- **`WebSocket::subscribe(topic)`** — subscribes the connection to an in-process pub/sub topic
- **`WebSocket::publish(topic, message)`** — broadcasts a message to all other subscribers on the same topic
- **`WebSocket::topic()`** — pre-populated from the first path parameter (`{room}`)
- **Self-publish suppression** — the sender does not receive its own broadcast

---

## API reference

| Type | Header | Docs |
|---|---|---|
| `aevox::WebSocket` | `<aevox/websocket.hpp>` | [WebSocket](../api/websocket.md) |
| `aevox::WebSocketHandler` | `<aevox/websocket_handler.hpp>` | [WebSocket](../api/websocket.md) |
| `aevox::App::ws()` | `<aevox/app.hpp>` | [Router and App](../api/router.md) |

---

## See Also

- [User Guide — WebSocket](../guide/websocket.md) — full guide to WebSocket routes, messaging, and pub/sub
- [Hello World example](hello-world.md) — the simplest possible Aevox server
