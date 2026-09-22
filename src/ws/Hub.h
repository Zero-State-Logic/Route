#pragma once
#include <drogon/WebSocketConnection.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace route {

// Topic-based pub/sub over WebSocket connections.
// Topics used by Route: "seats:<tripId>", "user:<userId>", "ops:dashboard".
class Hub {
  public:
    static Hub &instance();

    void subscribe(const drogon::WebSocketConnectionPtr &conn, const std::string &topic);
    void unsubscribeAll(const drogon::WebSocketConnectionPtr &conn);
    void publish(const std::string &topic, const std::string &message);

  private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_set<drogon::WebSocketConnectionPtr>> topics_;
};

}  // namespace route
