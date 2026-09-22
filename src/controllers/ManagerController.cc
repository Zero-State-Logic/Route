#include <drogon/HttpController.h>
#include <drogon/drogon.h>

#include <chrono>

#include "core/ApiResponse.h"
#include "core/Pricing.h"
#include "core/Surge.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

// Operator-console endpoints. All require a valid token (JwtAuthFilter) AND a
// manager/admin role (checked in-handler from the role attribute the filter set).
class ManagerController : public HttpController<ManagerController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ManagerController::rides, "/api/manager/rides", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::rentals, "/api/manager/rentals", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::fleet, "/api/manager/fleet", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::vehicleStatus, "/api/manager/vehicles/{1}/status", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::cancelRide, "/api/manager/rides/{1}/cancel", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::endRental, "/api/manager/rentals/{1}/end", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::getSurge, "/api/manager/surge", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::setSurge, "/api/manager/surge", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::autoSurge, "/api/manager/surge/auto", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(ManagerController::liveUnits, "/api/manager/live/units", Get, "route::JwtAuthFilter");
    METHOD_LIST_END

    Task<> rides(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> rentals(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> fleet(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> vehicleStatus(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> cancelRide(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> endRental(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> getSurge(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> setSurge(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> autoSurge(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> liveUnits(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
};

namespace {
bool isManager(const HttpRequestPtr &req) {
    auto role = req->attributes()->get<std::string>("role");
    return role == "manager" || role == "admin";
}
std::string genReceiptNo() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    std::string s = std::to_string(ms);
    return "RT-" + s.substr(s.size() - 8);
}
void pushJson(const std::string &topic, const Json::Value &ev) {
    Json::StreamWriterBuilder w; w["indentation"] = "";
    Hub::instance().publish(topic, Json::writeString(w, ev));
}
}  // namespace

Task<> ManagerController::rides(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT rr.id, rr.origin, rr.dest, rr.mode, rr.status, rr.driver_label, "
            "       rr.fare_estimate, rr.created_at, u.full_name AS rider "
            "FROM ride_requests rr LEFT JOIN users u ON u.id=rr.user_id "
            "ORDER BY rr.created_at DESC LIMIT 100");
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value r;
            r["ride_id"] = row["id"].as<int>();
            r["rider"] = row["rider"].isNull() ? "" : row["rider"].as<std::string>();
            r["origin"] = row["origin"].as<std::string>();
            r["dest"] = row["dest"].as<std::string>();
            r["mode"] = row["mode"].as<std::string>();
            r["status"] = row["status"].as<std::string>();
            r["driver_label"] = row["driver_label"].isNull() ? "" : row["driver_label"].as<std::string>();
            r["fare_estimate"] = row["fare_estimate"].isNull() ? 0.0 : row["fare_estimate"].as<double>();
            r["created_at"] = row["created_at"].as<std::string>();
            arr.append(r);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        LOG_ERROR << "manager rides failed: " << e.what();
        callback(jsonErr("Could not load rides", k500InternalServerError));
    }
    co_return;
}

Task<> ManagerController::rentals(HttpRequestPtr req,
                                std::function<void(const HttpResponsePtr &)> callback) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT r.id, r.status, r.started_at, r.ended_at, r.minutes, r.amount, "
            "       v.kind, v.code, u.full_name AS rider "
            "FROM rentals r JOIN rental_vehicles v ON v.id=r.vehicle_id "
            "LEFT JOIN users u ON u.id=r.user_id "
            "ORDER BY r.started_at DESC LIMIT 100");
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value r;
            r["rental_id"] = row["id"].as<int>();
            r["status"] = row["status"].as<std::string>();
            r["kind"] = row["kind"].as<std::string>();
            r["code"] = row["code"].as<std::string>();
            r["rider"] = row["rider"].isNull() ? "" : row["rider"].as<std::string>();
            r["started_at"] = row["started_at"].as<std::string>();
            r["ended_at"] = row["ended_at"].isNull() ? "" : row["ended_at"].as<std::string>();
            r["minutes"] = row["minutes"].isNull() ? 0 : row["minutes"].as<int>();
            r["amount"] = row["amount"].isNull() ? 0.0 : row["amount"].as<double>();
            arr.append(r);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        LOG_ERROR << "manager rentals failed: " << e.what();
        callback(jsonErr("Could not load rentals", k500InternalServerError));
    }
    co_return;
}

Task<> ManagerController::fleet(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT id, kind, code, status FROM rental_vehicles ORDER BY kind, code");
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value v;
            v["id"] = row["id"].as<int>();
            v["kind"] = row["kind"].as<std::string>();
            v["code"] = row["code"].as<std::string>();
            v["status"] = row["status"].as<std::string>();
            arr.append(v);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load fleet", k500InternalServerError));
    }
    co_return;
}

Task<> ManagerController::vehicleStatus(HttpRequestPtr req,
                                      std::function<void(const HttpResponsePtr &)> callback, int id) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto body = req->getJsonObject();
    std::string status = body ? (*body).get("status", "").asString() : "";
    if (status != "available" && status != "in_use" && status != "maintenance") {
        callback(jsonErr("status must be available, in_use or maintenance"));
        co_return;
    }
    auto db = app().getDbClient();
    try {
        co_await db->execSqlCoro("UPDATE rental_vehicles SET status=$2 WHERE id=$1", id, status);
        Json::Value d; d["id"] = id; d["status"] = status;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not update vehicle", k500InternalServerError));
    }
    co_return;
}

Task<> ManagerController::cancelRide(HttpRequestPtr req,
                                   std::function<void(const HttpResponsePtr &)> callback, int id) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto db = app().getDbClient();
    try {
        auto r = co_await db->execSqlCoro(
            "UPDATE ride_requests SET status='cancelled' "
            "WHERE id=$1 AND status NOT IN ('completed','cancelled') RETURNING user_id", id);
        if (r.size() == 0) { callback(jsonErr("Ride not found or already closed", k409Conflict)); co_return; }
        long uid = r[0]["user_id"].as<long>();
        Json::Value ev; ev["type"] = "ride.status"; ev["ride_id"] = id; ev["status"] = "cancelled";
        pushJson("user:" + std::to_string(uid), ev);
        Json::Value d; d["ride_id"] = id; d["status"] = "cancelled";
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not cancel ride", k500InternalServerError));
    }
    co_return;
}

Task<> ManagerController::endRental(HttpRequestPtr req,
                                  std::function<void(const HttpResponsePtr &)> callback, int id) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    const double dummy = 0; (void)dummy;
    auto db = app().getDbClient();
    try {
        auto trans = co_await db->newTransactionCoro();
        auto r = co_await trans->execSqlCoro(
            "SELECT r.user_id, r.vehicle_id, v.per_min_rate, v.unlock_fee, "
            "       CEIL(EXTRACT(EPOCH FROM (now()-r.started_at))/60.0)::int AS minutes "
            "FROM rentals r JOIN rental_vehicles v ON v.id=r.vehicle_id "
            "WHERE r.id=$1 AND r.status='active' FOR UPDATE", id);
        if (r.size() == 0) { trans->rollback(); callback(jsonErr("Active rental not found", k404NotFound)); co_return; }
        long uid = r[0]["user_id"].as<long>();
        int vehicleId = r[0]["vehicle_id"].as<int>();
        int minutes = r[0]["minutes"].as<int>();
        double amount = pricing::rentalAmount(minutes, r[0]["per_min_rate"].as<double>(), r[0]["unlock_fee"].as<double>());
        co_await trans->execSqlCoro(
            "UPDATE rentals SET status='completed', ended_at=now(), minutes=$2, amount=$3 WHERE id=$1",
            id, minutes, amount);
        co_await trans->execSqlCoro("UPDATE rental_vehicles SET status='available' WHERE id=$1", vehicleId);
        std::string receiptNo = genReceiptNo();
        Json::Value payload; payload["receipt_no"] = receiptNo; payload["type"] = "rental";
        payload["rental_id"] = id; payload["minutes"] = minutes; payload["amount"] = amount;
        payload["ended_by"] = "operator";
        Json::StreamWriterBuilder w; w["indentation"] = "";
        co_await trans->execSqlCoro(
            "INSERT INTO receipts (booking_id, receipt_no, amount, payload) VALUES (NULL,$1,$2,$3::jsonb)",
            receiptNo, amount, Json::writeString(w, payload));
        Json::Value ev; ev["type"] = "receipt.ready"; ev["receipt"] = payload;
        pushJson("user:" + std::to_string(uid), ev);
        Json::Value d; d["rental_id"] = id; d["minutes"] = minutes; d["amount"] = amount; d["receipt_no"] = receiptNo;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        LOG_ERROR << "manager end rental failed: " << e.what();
        callback(jsonErr("Could not end rental", k500InternalServerError));
    }
    co_return;
}

Task<> ManagerController::liveUnits(HttpRequestPtr req,
                                   std::function<void(const HttpResponsePtr &)> callback) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT id,name,vehicle,plate,rating,status,lat,lng,last_seen FROM drivers ORDER BY id");
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value v;
            v["id"] = row["id"].as<int>();
            v["name"] = row["name"].as<std::string>();
            v["vehicle"] = row["vehicle"].as<std::string>();
            v["plate"] = row["plate"].as<std::string>();
            v["rating"] = row["rating"].as<double>();
            v["status"] = row["status"].as<std::string>();
            if (!row["lat"].isNull()) { v["lat"] = row["lat"].as<double>(); v["lng"] = row["lng"].as<double>(); }
            arr.append(v);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load units", k500InternalServerError));
    }
    co_return;
}

static void broadcastSurge() {
    Json::Value ev;
    ev["type"] = "surge.update";
    ev["multiplier"] = Surge::instance().current();
    ev["manual"] = Surge::instance().isManual();
    Json::StreamWriterBuilder w; w["indentation"] = "";
    std::string msg = Json::writeString(w, ev);
    Hub::instance().publish("pricing", msg);
    Hub::instance().publish("ops:dashboard", msg);
}

Task<> ManagerController::getSurge(HttpRequestPtr req,
                                  std::function<void(const HttpResponsePtr &)> callback) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    Json::Value d;
    d["multiplier"] = Surge::instance().current();
    d["manual"] = Surge::instance().isManual();
    callback(jsonOk(d));
    co_return;
}

Task<> ManagerController::setSurge(HttpRequestPtr req,
                                  std::function<void(const HttpResponsePtr &)> callback) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto body = req->getJsonObject();
    double m = body ? (*body).get("multiplier", 1.0).asDouble() : 1.0;
    Surge::instance().setManual(m);
    broadcastSurge();
    Json::Value d;
    d["multiplier"] = Surge::instance().current();
    d["manual"] = true;
    callback(jsonOk(d));
    co_return;
}

Task<> ManagerController::autoSurge(HttpRequestPtr req,
                                   std::function<void(const HttpResponsePtr &)> callback) {
    if (!isManager(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    Surge::instance().resumeAuto();
    broadcastSurge();
    Json::Value d;
    d["multiplier"] = Surge::instance().current();
    d["manual"] = false;
    callback(jsonOk(d));
    co_return;
}

}  // namespace route
