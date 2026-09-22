#include "controllers/BookingController.h"

#include <drogon/drogon.h>

#include <chrono>

#include "core/ApiResponse.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

namespace {
std::string genReceiptNo() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    std::string s = std::to_string(ms);
    return "RT-" + s.substr(s.size() - 8);
}
}  // namespace

Task<> BookingController::create(HttpRequestPtr req,
                                std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    if (!body || !(*body).isMember("trip_id") || !(*body)["seats"].isArray() ||
        (*body)["seats"].empty()) {
        callback(jsonErr("Provide trip_id and a non-empty seats array"));
        co_return;
    }
    int tripId = (*body)["trip_id"].asInt();
    std::vector<std::string> seats;
    for (const auto &s : (*body)["seats"]) seats.push_back(s.asString());

    const double serviceFee =
        app().getCustomConfig().get("service_fee", 0.40).asDouble();

    auto db = app().getDbClient();
    try {
        // Fare + validation happen inside a transaction with row locks so two
        // riders cannot grab the same seat concurrently.
        auto trans = co_await db->newTransactionCoro();

        auto fareRes = co_await trans->execSqlCoro(
            "SELECT fare FROM trips WHERE id=$1", tripId);
        if (fareRes.size() == 0) {
            callback(jsonErr("Trip not found", k404NotFound));
            co_return;  // transaction rolls back on destruction
        }
        double fare = fareRes[0]["fare"].as<double>();

        bool conflict = false;
        std::string conflictSeat;
        for (const auto &label : seats) {
            auto row = co_await trans->execSqlCoro(
                "SELECT status FROM trip_seats WHERE trip_id=$1 AND seat_label=$2 "
                "FOR UPDATE",
                tripId, label);
            if (row.size() == 0 || row[0]["status"].as<std::string>() != "free") {
                conflict = true;
                conflictSeat = label;
                break;
            }
        }
        if (conflict) {
            trans->rollback();
            callback(jsonErr("Seat " + conflictSeat + " is no longer available",
                             k409Conflict));
            co_return;
        }

        double total = fare * static_cast<double>(seats.size()) + serviceFee;

        auto bk = co_await trans->execSqlCoro(
            "INSERT INTO bookings (user_id, trip_id, status, total) "
            "VALUES ($1,$2,'confirmed',$3) RETURNING id",
            uid, tripId, total);
        long bookingId = bk[0]["id"].as<long>();

        for (const auto &label : seats) {
            co_await trans->execSqlCoro(
                "UPDATE trip_seats SET status='booked' WHERE trip_id=$1 AND seat_label=$2",
                tripId, label);
            co_await trans->execSqlCoro(
                "INSERT INTO booking_seats (booking_id, seat_label) VALUES ($1,$2)",
                bookingId, label);
        }

        std::string receiptNo = genReceiptNo();
        Json::Value payload;
        payload["receipt_no"] = receiptNo;
        payload["booking_id"] = static_cast<Json::Int64>(bookingId);
        payload["trip_id"] = tripId;
        Json::Value seatArr(Json::arrayValue);
        for (const auto &label : seats) seatArr.append(label);
        payload["seats"] = seatArr;
        payload["fare"] = fare;
        payload["service_fee"] = serviceFee;
        payload["total"] = total;
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        std::string payloadStr = Json::writeString(w, payload);

        co_await trans->execSqlCoro(
            "INSERT INTO receipts (booking_id, receipt_no, amount, payload) "
            "VALUES ($1,$2,$3,$4::jsonb)",
            bookingId, receiptNo, total, payloadStr);

        // trans commits on scope exit below; broadcast + respond afterwards.
        Json::Value data;
        data["booking_id"] = static_cast<Json::Int64>(bookingId);
        data["receipt_no"] = receiptNo;
        data["total"] = total;
        data["seats"] = seatArr;

        // Realtime: tell everyone watching this trip that seats are now booked,
        // and push the fresh receipt to this user's channel.
        for (const auto &label : seats) {
            Json::Value ev;
            ev["type"] = "seat.update";
            ev["trip_id"] = tripId;
            ev["seat_label"] = label;
            ev["status"] = "booked";
            Hub::instance().publish("seats:" + std::to_string(tripId),
                                    Json::writeString(w, ev));
        }
        Json::Value receiptEv;
        receiptEv["type"] = "receipt.ready";
        receiptEv["receipt"] = payload;
        Hub::instance().publish("user:" + std::to_string(uid),
                                Json::writeString(w, receiptEv));

        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "booking failed: " << e.what();
        callback(jsonErr("Could not complete booking", k500InternalServerError));
    }
    co_return;
}

Task<> BookingController::list(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT b.id, b.trip_id, b.status, b.total, b.created_at, "
            "       r.name AS route, t.vehicle_label "
            "FROM bookings b JOIN trips t ON t.id=b.trip_id "
            "JOIN routes r ON r.id=t.route_id "
            "WHERE b.user_id=$1 ORDER BY b.created_at DESC",
            uid);
        Json::Value arr(Json::arrayValue);
        for (const auto &row : res) {
            Json::Value b;
            b["id"] = row["id"].as<int>();
            b["trip_id"] = row["trip_id"].as<int>();
            b["status"] = row["status"].as<std::string>();
            b["total"] = row["total"].as<double>();
            b["created_at"] = row["created_at"].as<std::string>();
            b["route"] = row["route"].as<std::string>();
            b["vehicle_label"] = row["vehicle_label"].as<std::string>();
            arr.append(b);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load bookings", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
