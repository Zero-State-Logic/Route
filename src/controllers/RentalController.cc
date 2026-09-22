#include "controllers/RentalController.h"

#include <drogon/drogon.h>

#include <chrono>

#include "core/ApiResponse.h"
#include "core/Pricing.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

namespace {
std::string genReceiptNo() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    std::string s = std::to_string(ms);
    return "RT-" + s.substr(s.size() - 8);
}
}  // namespace

Task<> RentalController::vehicles(HttpRequestPtr req,
                                 std::function<void(const HttpResponsePtr &)> callback) {
    const std::string kind = req->getParameter("kind");
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT id, kind, code, status, lat, lng, unlock_fee, per_min_rate "
            "FROM rental_vehicles WHERE status='available' AND ($1='' OR kind=$1) "
            "ORDER BY kind, code",
            kind);
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value v;
            v["id"] = row["id"].as<int>();
            v["kind"] = row["kind"].as<std::string>();
            v["code"] = row["code"].as<std::string>();
            v["lat"] = row["lat"].as<double>();
            v["lng"] = row["lng"].as<double>();
            v["unlock_fee"] = row["unlock_fee"].as<double>();
            v["per_min_rate"] = row["per_min_rate"].as<double>();
            arr.append(v);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load vehicles", k500InternalServerError));
    }
    co_return;
}

Task<> RentalController::start(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    if (!body || !(*body).isMember("vehicle_id")) {
        callback(jsonErr("Provide vehicle_id"));
        co_return;
    }
    int vehicleId = (*body)["vehicle_id"].asInt();
    auto db = app().getDbClient();
    try {
        auto trans = co_await db->newTransactionCoro();
        auto v = co_await trans->execSqlCoro(
            "SELECT status, kind, code FROM rental_vehicles WHERE id=$1 FOR UPDATE",
            vehicleId);
        if (v.size() == 0 || v[0]["status"].as<std::string>() != "available") {
            trans->rollback();
            callback(jsonErr("Vehicle is not available", k409Conflict));
            co_return;
        }
        co_await trans->execSqlCoro(
            "UPDATE rental_vehicles SET status='in_use' WHERE id=$1", vehicleId);
        auto r = co_await trans->execSqlCoro(
            "INSERT INTO rentals (user_id, vehicle_id, status) VALUES ($1,$2,'active') "
            "RETURNING id, started_at",
            uid, vehicleId);
        Json::Value data;
        data["rental_id"] = r[0]["id"].as<int>();
        data["vehicle_id"] = vehicleId;
        data["kind"] = v[0]["kind"].as<std::string>();
        data["code"] = v[0]["code"].as<std::string>();
        data["started_at"] = r[0]["started_at"].as<std::string>();
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "rental start failed: " << e.what();
        callback(jsonErr("Could not start rental", k500InternalServerError));
    }
    co_return;
}

Task<> RentalController::stop(HttpRequestPtr req,
                             std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    if (!body || !(*body).isMember("rental_id")) {
        callback(jsonErr("Provide rental_id"));
        co_return;
    }
    int rentalId = (*body)["rental_id"].asInt();
    auto db = app().getDbClient();
    try {
        auto trans = co_await db->newTransactionCoro();
        auto r = co_await trans->execSqlCoro(
            "SELECT r.vehicle_id, v.per_min_rate, v.unlock_fee, "
            "       CEIL(EXTRACT(EPOCH FROM (now()-r.started_at))/60.0)::int AS minutes "
            "FROM rentals r JOIN rental_vehicles v ON v.id=r.vehicle_id "
            "WHERE r.id=$1 AND r.user_id=$2 AND r.status='active' FOR UPDATE",
            rentalId, uid);
        if (r.size() == 0) {
            trans->rollback();
            callback(jsonErr("Active rental not found", k404NotFound));
            co_return;
        }
        int vehicleId = r[0]["vehicle_id"].as<int>();
        int minutes = r[0]["minutes"].as<int>();
        double perMin = r[0]["per_min_rate"].as<double>();
        double unlock = r[0]["unlock_fee"].as<double>();
        double amount = pricing::rentalAmount(minutes, perMin, unlock);

        co_await trans->execSqlCoro(
            "UPDATE rentals SET status='completed', ended_at=now(), minutes=$2, amount=$3 "
            "WHERE id=$1",
            rentalId, minutes, amount);
        co_await trans->execSqlCoro(
            "UPDATE rental_vehicles SET status='available' WHERE id=$1", vehicleId);

        std::string receiptNo = genReceiptNo();
        Json::Value payload;
        payload["receipt_no"] = receiptNo;
        payload["type"] = "rental";
        payload["rental_id"] = rentalId;
        payload["minutes"] = minutes;
        payload["amount"] = amount;
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        std::string payloadStr = Json::writeString(w, payload);
        co_await trans->execSqlCoro(
            "INSERT INTO receipts (booking_id, receipt_no, amount, payload) "
            "VALUES (NULL,$1,$2,$3::jsonb)",
            receiptNo, amount, payloadStr);

        Json::Value data;
        data["rental_id"] = rentalId;
        data["minutes"] = minutes;
        data["amount"] = amount;
        data["receipt_no"] = receiptNo;

        Json::Value ev;
        ev["type"] = "receipt.ready";
        ev["receipt"] = payload;
        Hub::instance().publish("user:" + std::to_string(uid), Json::writeString(w, ev));

        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "rental stop failed: " << e.what();
        callback(jsonErr("Could not stop rental", k500InternalServerError));
    }
    co_return;
}

Task<> RentalController::active(HttpRequestPtr req,
                               std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT r.id, r.started_at, v.kind, v.code, v.per_min_rate, v.unlock_fee, "
            "       CEIL(EXTRACT(EPOCH FROM (now()-r.started_at))/60.0)::int AS minutes "
            "FROM rentals r JOIN rental_vehicles v ON v.id=r.vehicle_id "
            "WHERE r.user_id=$1 AND r.status='active'",
            uid);
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value item;
            int minutes = row["minutes"].as<int>();
            item["rental_id"] = row["id"].as<int>();
            item["kind"] = row["kind"].as<std::string>();
            item["code"] = row["code"].as<std::string>();
            item["started_at"] = row["started_at"].as<std::string>();
            item["minutes"] = minutes;
            item["per_min_rate"] = row["per_min_rate"].as<double>();
            item["unlock_fee"] = row["unlock_fee"].as<double>();
            item["running_amount"] = pricing::rentalAmount(
                minutes, row["per_min_rate"].as<double>(), row["unlock_fee"].as<double>());
            arr.append(item);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load rentals", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
