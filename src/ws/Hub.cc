#include "ws/Hub.h"

namespace route {

Hub &Hub::instance() {
    static Hub hub;
    return hub;
}

void Hub::subscribe(const drogon::WebSocketConnectionPtr &conn, const std::string &topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    topics_[topic].insert(conn);
}

void Hub::unsubscribeAll(const drogon::WebSocketConnectionPtr &conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &entry : topics_) entry.second.erase(conn);
}

void Hub::publish(const std::string &topic, const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = topics_.find(topic);
    if (it == topics_.end()) return;
    for (const auto &conn : it->second) {
        if (conn->connected()) conn->send(message);
    }
}

}  // namespace route
