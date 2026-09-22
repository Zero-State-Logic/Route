#pragma once
#include <drogon/WebSocketController.h>

namespace route {

// WebSocket endpoint for realtime updates. Clients send:
//   {"action":"subscribe","topic":"seats:1"}
class LiveController : public drogon::WebSocketController<LiveController> {
  public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr &,
                          std::string &&message,
                          const drogon::WebSocketMessageType &) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &) override;
    void handleNewConnection(const drogon::HttpRequestPtr &,
                             const drogon::WebSocketConnectionPtr &) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/live");
    WS_PATH_LIST_END
};

}  // namespace route
