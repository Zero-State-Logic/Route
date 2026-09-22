#include <drogon/HttpController.h>
#include <drogon/drogon.h>

#include <string>

#include "core/ApiResponse.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

// Persisted payments + auditable wallet. intent -> confirm; wallet method debits
// the balance inside a locked transaction and appends a ledger row.
class PaymentController : public HttpController<PaymentController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PaymentController::intent, "/api/payments/intent", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(PaymentController::confirm, "/api/payments/{1}/confirm", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(PaymentController::wallet, "/api/wallet", Get, "route::JwtAuthFilter");
    METHOD_LIST_END

    Task<> intent(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> confirm(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> wallet(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
};

Task<> PaymentController::intent(HttpRequestPtr req,
                                std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    double amount = body ? (*body).get("amount", 0.0).asDouble() : 0.0;
    if (amount <= 0) { callback(jsonErr("A positive amount is required")); co_return; }
    std::string currency = body ? (*body).get("currency", "PKR").asString() : "PKR";
    std::string symbol = body ? (*body).get("symbol", "Rs").asString() : "Rs";
    std::string ref = body ? (*body).get("ref", "").asString() : "";
    auto db = app().getDbClient();
    try {
        auto p = co_await db->execSqlCoro(
            "INSERT INTO payments (user_id, amount, currency, ref) VALUES ($1,$2,$3,$4) RETURNING id",
            uid, amount, currency, ref);
        Json::Value d;
        d["payment_id"] = p[0]["id"].as<int>();
        d["amount"] = amount;
        d["currency"] = currency;
        d["symbol"] = symbol;
        d["status"] = "requires_confirmation";
        Json::Value methods(Json::arrayValue);
        for (auto m : {"card", "wallet", "cash", "qr"}) methods.append(m);
        d["methods"] = methods;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        LOG_ERROR << "intent failed: " << e.what();
        callback(jsonErr("Could not create payment", k500InternalServerError));
    }
    co_return;
}

Task<> PaymentController::confirm(HttpRequestPtr req,
                                 std::function<void(const HttpResponsePtr &)> callback, int id) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    std::string method = body ? (*body).get("method", "").asString() : "";
    if (method != "card" && method != "wallet" && method != "cash" && method != "qr") {
        callback(jsonErr("Choose a payment method"));
        co_return;
    }
    auto db = app().getDbClient();
    try {
        auto p = co_await db->execSqlCoro(
            "SELECT amount, status FROM payments WHERE id=$1 AND user_id=$2", id, uid);
        if (p.size() == 0) { callback(jsonErr("Payment not found", k404NotFound)); co_return; }
        if (p[0]["status"].as<std::string>() != "requires_confirmation") {
            callback(jsonErr("Payment already processed", k409Conflict));
            co_return;
        }
        double amount = p[0]["amount"].as<double>();

        if (method == "card") {
            std::string raw = body ? (*body).get("card", "").asString() : "";
            std::string digits;
            for (char c : raw) if (c >= '0' && c <= '9') digits.push_back(c);
            if (digits.size() < 12 || digits.size() > 19) {
                callback(jsonErr("Enter a valid card number", k422UnprocessableEntity));
                co_return;
            }
        } else if (method == "wallet") {
            auto trans = co_await db->newTransactionCoro();
            co_await trans->execSqlCoro(
                "INSERT INTO wallets (user_id, balance) VALUES ($1, 0) ON CONFLICT (user_id) DO NOTHING", uid);
            auto w = co_await trans->execSqlCoro(
                "SELECT balance FROM wallets WHERE user_id=$1 FOR UPDATE", uid);
            double bal = w.size() ? w[0]["balance"].as<double>() : 0.0;
            if (bal + 1e-9 < amount) {
                trans->rollback();
                callback(jsonErr("Insufficient wallet balance", k402PaymentRequired));
                co_return;
            }
            double after = bal - amount;
            co_await trans->execSqlCoro("UPDATE wallets SET balance=$2 WHERE user_id=$1", uid, after);
            co_await trans->execSqlCoro(
                "INSERT INTO wallet_ledger (user_id, delta, reason, ref, balance_after) "
                "VALUES ($1,$2,'ride',$3,$4)",
                uid, -amount, std::to_string(id), after);
        }

        co_await db->execSqlCoro(
            "UPDATE payments SET status='succeeded', method=$2, confirmed_at=now() WHERE id=$1",
            id, method);

        Json::Value ev;
        ev["type"] = "payment.status";
        ev["payment_id"] = id;
        ev["status"] = "succeeded";
        Json::StreamWriterBuilder w; w["indentation"] = "";
        Hub::instance().publish("user:" + std::to_string(uid), Json::writeString(w, ev));

        Json::Value d;
        d["payment_id"] = id;
        d["status"] = "succeeded";
        d["method"] = method;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        LOG_ERROR << "confirm failed: " << e.what();
        callback(jsonErr("Payment failed", k500InternalServerError));
    }
    co_return;
}

Task<> PaymentController::wallet(HttpRequestPtr req,
                                std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto db = app().getDbClient();
    try {
        // New users get a demo balance on first look.
        co_await db->execSqlCoro(
            "INSERT INTO wallets (user_id, balance) VALUES ($1, 5000) ON CONFLICT (user_id) DO NOTHING", uid);
        auto w = co_await db->execSqlCoro("SELECT balance, currency FROM wallets WHERE user_id=$1", uid);
        auto led = co_await db->execSqlCoro(
            "SELECT delta, reason, balance_after, created_at FROM wallet_ledger "
            "WHERE user_id=$1 ORDER BY id DESC LIMIT 10", uid);
        Json::Value d;
        d["balance"] = w.size() ? w[0]["balance"].as<double>() : 0.0;
        d["currency"] = w.size() ? w[0]["currency"].as<std::string>() : "PKR";
        d["symbol"] = "Rs";
        Json::Value arr(Json::arrayValue);
        for (const auto &l : led) {
            Json::Value e;
            e["delta"] = l["delta"].as<double>();
            e["reason"] = l["reason"].isNull() ? "" : l["reason"].as<std::string>();
            e["balance_after"] = l["balance_after"].as<double>();
            e["created_at"] = l["created_at"].as<std::string>();
            arr.append(e);
        }
        d["ledger"] = arr;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load wallet", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
