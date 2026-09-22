#include <drogon/HttpController.h>
#include <drogon/drogon.h>

#include <chrono>
#include <optional>

#include "core/ApiResponse.h"
#include "core/Pricing.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

// On-demand dispatch for taxis and cabs. A driver is auto-assigned, then the
// ride status advances (enroute -> arrived) on timers, pushed over WebSocket.
class DispatchController : public HttpController<DispatchController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DispatchController::request, "/api/rides", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(DispatchController::mine, "/api/rides", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(DispatchController::complete, "/api/rides/{1}/complete", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(DispatchController::rate, "/api/rides/{1}/rate", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(DispatchController::tip, "/api/rides/{1}/tip", Post, "route::JwtAuthFilter");
    METHOD_LIST_END

    Task<> request(HttpRequestPtr req,
                   std::function<void(const HttpResponsePtr &)> callback);
    Task<> mine(HttpRequestPtr req,
                std::function<void(const HttpResponsePtr &)> callback);
    Task<> complete(HttpRequestPtr req,
                    std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> rate(HttpRequestPtr req,
                std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> tip(HttpRequestPtr req,
               std::function<void(const HttpResponsePtr &)> callback, int id);
};

namespace {
void pushRideStatus(long uid, int rideId, const std::string &status) {
    Json::Value ev;
    ev["type"] = "ride.status";
    ev["ride_id"] = rideId;
    ev["status"] = status;
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    Hub::instance().publish("user:" + std::to_string(uid), Json::writeString(w, ev));
}
}  // namespace

Task<> DispatchController::request(HttpRequestPtr req,
                                  std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    if (!body || !(*body).isMember("origin") || !(*body).isMember("dest")) {
        callback(jsonErr("Provide origin and dest"));
        co_return;
    }
    std::string origin = (*body)["origin"].asString();
    std::string dest = (*body)["dest"].asString();
    std::string mode = (*body).get("mode", "Taxi").asString();
    if (mode != "Taxi" && mode != "Cab") mode = "Taxi";
    double estimate = (body && (*body).isMember("fare_estimate"))
                          ? (*body)["fare_estimate"].asDouble()
                          : pricing::rideEstimate(mode);
    bool hasCoords = (*body).isMember("origin_lat") && (*body).isMember("origin_lng") &&
                     (*body).isMember("dest_lat") && (*body).isMember("dest_lng");
    double oLat = 0, oLng = 0, dLat = 0, dLng = 0, dsLat = 0, dsLng = 0;
    if (hasCoords) {
        oLat = (*body)["origin_lat"].asDouble();
        oLng = (*body)["origin_lng"].asDouble();
        dLat = (*body)["dest_lat"].asDouble();
        dLng = (*body)["dest_lng"].asDouble();
    }

    auto db = app().getDbClient();
    try {
        // Assign the nearest available driver from the real fleet.
        std::optional<int> driverId;
        std::string driver = "No driver available";
        drogon::orm::Result dr = hasCoords
            ? co_await db->execSqlCoro(
                  "SELECT id,name,vehicle,plate,rating,lat,lng FROM drivers "
                  "WHERE status='online' "
                  "ORDER BY (lat-$1)*(lat-$1)+(lng-$2)*(lng-$2) LIMIT 1", oLat, oLng)
            : co_await db->execSqlCoro(
                  "SELECT id,name,vehicle,plate,rating,lat,lng FROM drivers "
                  "WHERE status='online' LIMIT 1");
        if (dr.size() > 0) {
            driverId = dr[0]["id"].as<int>();
            driver = dr[0]["name"].as<std::string>() + " \xC2\xB7 " +
                     dr[0]["vehicle"].as<std::string>() + " \xC2\xB7 " +
                     dr[0]["plate"].as<std::string>() + " \xC2\xB7 " +
                     dr[0]["rating"].as<std::string>();
            if (!dr[0]["lat"].isNull()) { dsLat = dr[0]["lat"].as<double>(); dsLng = dr[0]["lng"].as<double>(); }
            co_await db->execSqlCoro("UPDATE drivers SET status='on_trip' WHERE id=$1", *driverId);
        }
        if ((dsLat == 0 || dsLng == 0) && hasCoords) { dsLat = oLat - 0.0055; dsLng = oLng + 0.006; }

        int rideId;
        if (hasCoords) {
            auto r = co_await db->execSqlCoro(
                "INSERT INTO ride_requests (user_id, origin, dest, mode, status, "
                "driver_label, fare_estimate, origin_lat, origin_lng, dest_lat, dest_lng, "
                "driver_start_lat, driver_start_lng) "
                "VALUES ($1,$2,$3,$4,'assigned',$5,$6,$7,$8,$9,$10,$11,$12) RETURNING id",
                uid, origin, dest, mode, driver, estimate,
                oLat, oLng, dLat, dLng, dsLat, dsLng);
            rideId = r[0]["id"].as<int>();
        } else {
            auto r = co_await db->execSqlCoro(
                "INSERT INTO ride_requests (user_id, origin, dest, mode, status, "
                "driver_label, fare_estimate) VALUES ($1,$2,$3,$4,'assigned',$5,$6) "
                "RETURNING id",
                uid, origin, dest, mode, driver, estimate);
            rideId = r[0]["id"].as<int>();
        }
        if (driverId)
            co_await db->execSqlCoro("UPDATE ride_requests SET driver_id=$2 WHERE id=$1", rideId, *driverId);

        auto *loop = app().getLoop();
        loop->runAfter(3.0, [uid, rideId] {
            app().getDbClient()->execSqlAsync(
                "UPDATE ride_requests SET status='enroute' WHERE id=$1",
                [](const orm::Result &) {}, [](const orm::DrogonDbException &) {}, rideId);
            pushRideStatus(uid, rideId, "enroute");
        });
        loop->runAfter(8.0, [uid, rideId] {
            app().getDbClient()->execSqlAsync(
                "UPDATE ride_requests SET status='arrived' WHERE id=$1",
                [](const orm::Result &) {}, [](const orm::DrogonDbException &) {}, rideId);
            pushRideStatus(uid, rideId, "arrived");
        });

        Json::Value data;
        data["ride_id"] = rideId;
        data["mode"] = mode;
        data["driver_label"] = driver;
        if (driverId) data["driver_id"] = *driverId;
        data["fare_estimate"] = estimate;
        data["status"] = "assigned";
        if (hasCoords) {
            Json::Value ds;
            ds["lat"] = dsLat;
            ds["lng"] = dsLng;
            data["driver_start"] = ds;
        }
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "ride request failed: " << e.what();
        callback(jsonErr("Could not request ride", k500InternalServerError));
    }
    co_return;
}

Task<> DispatchController::mine(HttpRequestPtr req,
                               std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT id, origin, dest, mode, status, driver_label, fare_estimate, created_at, "
            "origin_lat, origin_lng, dest_lat, dest_lng, driver_start_lat, driver_start_lng "
            "FROM ride_requests WHERE user_id=$1 ORDER BY created_at DESC LIMIT 20",
            uid);
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value r;
            r["ride_id"] = row["id"].as<int>();
            r["origin"] = row["origin"].as<std::string>();
            r["dest"] = row["dest"].as<std::string>();
            r["mode"] = row["mode"].as<std::string>();
            r["status"] = row["status"].as<std::string>();
            r["driver_label"] = row["driver_label"].as<std::string>();
            r["fare_estimate"] = row["fare_estimate"].as<double>();
            r["created_at"] = row["created_at"].as<std::string>();
            if (!row["origin_lat"].isNull()) {
                r["origin_lat"] = row["origin_lat"].as<double>();
                r["origin_lng"] = row["origin_lng"].as<double>();
                r["dest_lat"] = row["dest_lat"].as<double>();
                r["dest_lng"] = row["dest_lng"].as<double>();
            }
            if (!row["driver_start_lat"].isNull()) {
                r["driver_start_lat"] = row["driver_start_lat"].as<double>();
                r["driver_start_lng"] = row["driver_start_lng"].as<double>();
            }
            arr.append(r);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load rides", k500InternalServerError));
    }
    co_return;
}

Task<> DispatchController::complete(HttpRequestPtr req,
                                   std::function<void(const HttpResponsePtr &)> callback, int id) {
    long uid = req->attributes()->get<long>("uid");
    auto db = app().getDbClient();
    try {
        auto r = co_await db->execSqlCoro(
            "UPDATE ride_requests SET status='completed', fare_final=fare_estimate "
            "WHERE id=$1 AND user_id=$2 AND status NOT IN ('completed','cancelled') "
            "RETURNING fare_estimate, mode, origin, dest",
            id, uid);
        if (r.size() == 0) {
            callback(jsonErr("Ride not found or already closed", k409Conflict));
            co_return;
        }
        double fare = r[0]["fare_estimate"].as<double>();
        co_await db->execSqlCoro(
            "UPDATE drivers SET status='online' "
            "WHERE id=(SELECT driver_id FROM ride_requests WHERE id=$1)", id);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();
        std::string s = std::to_string(ms);
        std::string receiptNo = "RT-" + s.substr(s.size() - 8);
        Json::Value payload;
        payload["receipt_no"] = receiptNo;
        payload["type"] = "ride";
        payload["ride_id"] = id;
        payload["mode"] = r[0]["mode"].as<std::string>();
        payload["origin"] = r[0]["origin"].as<std::string>();
        payload["dest"] = r[0]["dest"].as<std::string>();
        payload["amount"] = fare;
        payload["currency"] = "PKR";
        Json::StreamWriterBuilder w; w["indentation"] = "";
        std::string payloadStr = Json::writeString(w, payload);
        co_await db->execSqlCoro(
            "INSERT INTO receipts (booking_id, receipt_no, amount, payload) "
            "VALUES (NULL,$1,$2,$3::jsonb)",
            receiptNo, fare, payloadStr);
        Json::Value ev;
        ev["type"] = "ride.completed";
        ev["ride_id"] = id;
        ev["fare_final"] = fare;
        ev["receipt_no"] = receiptNo;
        Hub::instance().publish("user:" + std::to_string(uid), Json::writeString(w, ev));
        Json::Value rev;
        rev["type"] = "receipt.ready";
        rev["receipt"] = payload;
        Hub::instance().publish("user:" + std::to_string(uid), Json::writeString(w, rev));
        Json::Value d;
        d["ride_id"] = id;
        d["fare_final"] = fare;
        d["receipt_no"] = receiptNo;
        d["currency"] = "PKR";
        d["symbol"] = "Rs";
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        LOG_ERROR << "ride complete failed: " << e.what();
        callback(jsonErr("Could not complete ride", k500InternalServerError));
    }
    co_return;
}

Task<> DispatchController::rate(HttpRequestPtr req,
                               std::function<void(const HttpResponsePtr &)> callback, int id) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    int stars = body ? (*body).get("stars", 0).asInt() : 0;
    if (stars < 1 || stars > 5) { callback(jsonErr("stars must be 1-5")); co_return; }
    auto db = app().getDbClient();
    try {
        co_await db->execSqlCoro(
            "UPDATE ride_requests SET rating=$3 WHERE id=$1 AND user_id=$2", id, uid, stars);
        Json::Value d; d["ride_id"] = id; d["rating"] = stars;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not save rating", k500InternalServerError));
    }
    co_return;
}

Task<> DispatchController::tip(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback, int id) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    double amount = body ? (*body).get("amount", 0.0).asDouble() : 0.0;
    if (amount < 0) { callback(jsonErr("Invalid tip")); co_return; }
    auto db = app().getDbClient();
    try {
        co_await db->execSqlCoro(
            "UPDATE ride_requests SET tip=$3 WHERE id=$1 AND user_id=$2", id, uid, amount);
        Json::Value d; d["ride_id"] = id; d["tip"] = amount;
        callback(jsonOk(d));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not save tip", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
