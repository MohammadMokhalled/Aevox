// examples/websocket-chat/main.cpp
//
// Minimal WebSocket chat example demonstrating room-based broadcast with Aevox.
//
// Usage:
//   ./websocket-chat
//
// Connect multiple WebSocket clients to:
//   ws://localhost:8080/chat/{room}
//
// Any message sent to a room is broadcast to all other clients in that room.
// The sender does not receive its own message (self-publish suppression).

#include <aevox/app.hpp>
#include <aevox/websocket_handler.hpp>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string_view>

int main()
{
    aevox::App app;

    // Register a WebSocket route with a room path parameter.
    // Path pattern: /chat/{room}
    //   - The {room} segment is extracted automatically.
    //   - ws.topic() returns the room name for the current connection.
    app.ws("/chat/{room}",
           aevox::WebSocketHandler{
               .on_open =
                   [](aevox::WebSocket& ws) {
                       // Subscribe to the room derived from the first path parameter.
                       // ws.topic() is pre-populated from the first path segment by the framework.
                       ws.subscribe(ws.topic());
                       std::cout << std::format("[open]  client joined room '{}'\n", ws.topic());
                   },

               .on_message =
                   [](aevox::WebSocket& ws, std::string_view msg) {
                       // Broadcast to everyone in the same room.
                       // Self-publish is suppressed internally — the sender will NOT receive
                       // its own message.
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
