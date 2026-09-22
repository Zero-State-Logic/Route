#include "controllers/TripController.h"

#include <drogon/drogon.h>

#include "core/ApiResponse.h"

using namespace drogon;

namespace route {

Task<> TripController::list(HttpRequestPtr req,
                           std::function<void(const HttpResponsePtr &)> callback) {
    const std::string mode = req->getParameter("mode");
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT t.id, r.name AS route, t.vehicle_label, t.depart_at, "
            "       t.duration_min, t.fare, t.mode, t.seat_count, "
            "       (SELECT count(*) FROM trip_seats s "
            "        WHERE s.trip_id=t.id AND s.status='free') AS free "
            "FROM trips t JOIN routes r ON r.id=t.route_id "
            "WHERE ($1 = '' OR t.mode = $1) "
            "ORDER BY t.depart_at",
            mode);
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value t;
            t["id"] = row["id"].as<int>();
            t["route"] = row["route"].as<std::string>();
            t["vehicle_label"] = row["vehicle_label"].as<std::string>();
            t["depart_at"] = row["depart_at"].as<std::string>();
            t["duration_min"] = row["duration_min"].as<int>();
            t["fare"] = row["fare"].as<double>();
            t["mode"] = row["mode"].as<std::string>();
            t["seat_count"] = row["seat_count"].as<int>();
            t["free"] = row["free"].as<int>();
            arr.append(t);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        LOG_ERROR << "trip list failed: " << e.what();
        callback(jsonErr("Could not load trips", k500InternalServerError));
    }
    co_return;
}

Task<> TripController::seats(HttpRequestPtr req,
                            std::function<void(const HttpResponsePtr &)> callback,
                            int tripId) {
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT seat_label, status FROM trip_seats WHERE trip_id=$1 "
            "ORDER BY seat_label",
            tripId);
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value s;
            s["seat_label"] = row["seat_label"].as<std::string>();
            s["status"] = row["status"].as<std::string>();
            arr.append(s);
        }
        Json::Value data;
        data["trip_id"] = tripId;
        data["seats"] = arr;
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load seats", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
