#include <drogon/HttpController.h>
#include <drogon/drogon.h>

#include <string>

#include "core/ApiResponse.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

// Safety toolkit: rider SOS, share-trip (public read-only tracking), pickup PIN,
// and an operator safety desk.
class SafetyController : public HttpController<SafetyController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SafetyController::sos, "/api/rides/{1}/sos", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(SafetyController::safety, "/api/rides/{1}/safety", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(SafetyController::track, "/api/track/{1}", Get);
    ADD_METHOD_TO(SafetyController::managerSos, "/api/manager/sos", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(SafetyController::resolveSos, "/api/manager/sos/{1}/resolve", Post, "route::JwtAuthFilter");
    METHOD_LIST_END

    Task<> sos(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> safety(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> track(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, std::string token);
    Task<> managerSos(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> resolveSos(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
};

namespace {
bool mgr(const HttpRequestPtr &req) {
    auto role = req->attributes()->get<std::string>("role");
    return role == "manager" || role == "admin";
}
}  // namespace

Task<> SafetyController::sos(HttpRequestPtr req,
                            std::function<void(const HttpResponsePtr &)> callback, int id) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    double lat = body ? (*body).get("lat", 0.0).asDouble() : 0.0;
    double lng = body ? (*body).get("lng", 0.0).asDouble() : 0.0;
    std::string note = body ? (*body).get("note", "").asString() : "";
    auto db = app().getDbClient();
    try {
        co_await db->execSqlCoro(
            "INSERT INTO sos_events (user_id, ride_id, lat, lng, note) VALUES ($1,$2,$3,$4,$5)",
            uid, id, lat, lng, note);
        Json::Value ev;
        ev["type"] = "sos.raised";
        ev["ride_id"] = id;
        ev["user_id"] = static_cast<Json::Int64>(uid);
        ev["lat"] = lat;
        ev["lng"] = lng;
        Json::StreamWriterBuilder w; w["indentation"] = "";
        Hub::instance().publish("ops:safety", Json::writeString(w, ev));
        Hub::instance().publish("ops:dashboard", Json::writeString(w, ev));
        Json::Value d; d["ok"] = true;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        LOG_ERROR << "sos failed: " << e.what();
        callback(jsonErr("Could not raise SOS", k500InternalServerError));
    }
    co_return;
}

Task<> SafetyController::safety(HttpRequestPtr req,
                               std::function<void(const HttpResponsePtr &)> callback, int id) {
    long uid = req->attributes()->get<long>("uid");
    auto db = app().getDbClient();
    try {
        auto r = co_await db->execSqlCoro(
            "SELECT pin, share_token FROM ride_requests WHERE id=$1 AND user_id=$2", id, uid);
        if (r.size() == 0) { callback(jsonErr("Ride not found", k404NotFound)); co_return; }
        Json::Value d;
        d["pin"] = r[0]["pin"].isNull() ? "" : r[0]["pin"].as<std::string>();
        std::string token = r[0]["share_token"].isNull() ? "" : r[0]["share_token"].as<std::string>();
        d["share_token"] = token;
        d["share_url"] = "/track.html?t=" + token;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load safety info", k500InternalServerError));
    }
    co_return;
}

Task<> SafetyController::track(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback, std::string token) {
    auto db = app().getDbClient();
    try {
        auto r = co_await db->execSqlCoro(
            "SELECT status, driver_label, origin, dest, origin_lat, origin_lng, dest_lat, dest_lng, "
            "driver_start_lat, driver_start_lng FROM ride_requests WHERE share_token=$1", token);
        if (r.size() == 0) { callback(jsonErr("Trip not found", k404NotFound)); co_return; }
        Json::Value d;
        d["status"] = r[0]["status"].as<std::string>();
        d["driver_label"] = r[0]["driver_label"].isNull() ? "" : r[0]["driver_label"].as<std::string>();
        d["origin"] = r[0]["origin"].as<std::string>();
        d["dest"] = r[0]["dest"].as<std::string>();
        if (!r[0]["origin_lat"].isNull()) {
            d["origin_lat"] = r[0]["origin_lat"].as<double>();
            d["origin_lng"] = r[0]["origin_lng"].as<double>();
            d["dest_lat"] = r[0]["dest_lat"].as<double>();
            d["dest_lng"] = r[0]["dest_lng"].as<double>();
        }
        if (!r[0]["driver_start_lat"].isNull()) {
            d["driver_lat"] = r[0]["driver_start_lat"].as<double>();
            d["driver_lng"] = r[0]["driver_start_lng"].as<double>();
        }
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load trip", k500InternalServerError));
    }
    co_return;
}

Task<> SafetyController::managerSos(HttpRequestPtr req,
                                   std::function<void(const HttpResponsePtr &)> callback) {
    if (!mgr(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT s.id, s.ride_id, s.lat, s.lng, s.status, s.created_at, u.full_name AS rider "
            "FROM sos_events s LEFT JOIN users u ON u.id=s.user_id "
            "WHERE s.status='open' ORDER BY s.created_at DESC");
        Json::Value arr(Json::arrayValue);
        for (const auto &s : res) {
            Json::Value j;
            j["id"] = s["id"].as<int>();
            j["ride_id"] = s["ride_id"].isNull() ? 0 : s["ride_id"].as<int>();
            j["rider"] = s["rider"].isNull() ? "" : s["rider"].as<std::string>();
            if (!s["lat"].isNull()) { j["lat"] = s["lat"].as<double>(); j["lng"] = s["lng"].as<double>(); }
            j["created_at"] = s["created_at"].as<std::string>();
            arr.append(j);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load SOS", k500InternalServerError));
    }
    co_return;
}

Task<> SafetyController::resolveSos(HttpRequestPtr req,
                                   std::function<void(const HttpResponsePtr &)> callback, int id) {
    if (!mgr(req)) { callback(jsonErr("Manager access required", k403Forbidden)); co_return; }
    auto db = app().getDbClient();
    try {
        co_await db->execSqlCoro("UPDATE sos_events SET status='resolved' WHERE id=$1", id);
        Json::Value d; d["id"] = id; d["status"] = "resolved";
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not resolve", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
