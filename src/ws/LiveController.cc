#include "ws/LiveController.h"

#include <json/json.h>

#include <memory>
#include <string>

#include "auth/Jwt.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

// Per-connection identity, taken from the JWT passed as ?token= on connect.
struct LiveCtx {
    long uid = 0;
    std::string role;
};

void LiveController::handleNewConnection(const HttpRequestPtr &req,
                                         const WebSocketConnectionPtr &conn) {
    auto ctx = std::make_shared<LiveCtx>();
    auto claims = jwtutil::verify(req->getParameter("token"));
    if (claims) { ctx->uid = claims->userId; ctx->role = claims->role; }
    conn->setContext(ctx);
    conn->send(R"({"type":"welcome"})");
}

void LiveController::handleNewMessage(const WebSocketConnectionPtr &conn,
                                      std::string &&message,
                                      const WebSocketMessageType &type) {
    if (type != WebSocketMessageType::Text) return;
    Json::Value json;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(message.c_str(), message.c_str() + message.size(), &json, &errs))
        return;

    const std::string action = json.get("action", "").asString();
    const std::string topic = json.get("topic", "").asString();
    if (action != "subscribe" || topic.empty()) return;

    auto ctx = conn->getContext<LiveCtx>();
    bool allowed = true;
    if (topic.rfind("user:", 0) == 0) {
        // A rider may only subscribe to their own private channel.
        long want = 0;
        try { want = std::stol(topic.substr(5)); } catch (...) {}
        allowed = ctx && want != 0 && ctx->uid == want;
    } else if (topic.rfind("ops:", 0) == 0) {
        // Operations topics are manager/admin only.
        allowed = ctx && (ctx->role == "manager" || ctx->role == "admin");
    }
    // Public topics (seats:{id}, pricing) are open to any connection.

    if (!allowed) {
        conn->send(R"({"type":"error","message":"not authorized for topic"})");
        return;
    }
    Hub::instance().subscribe(conn, topic);
    conn->send(R"({"type":"subscribed","topic":")" + topic + R"("})");
}

void LiveController::handleConnectionClosed(const WebSocketConnectionPtr &conn) {
    Hub::instance().unsubscribeAll(conn);
}

}  // namespace route
