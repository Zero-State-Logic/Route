#include <drogon/HttpController.h>
#include <drogon/drogon.h>

#include <optional>
#include <string>

#include "core/ApiResponse.h"
#include "core/FareEngine.h"
#include "core/Surge.h"
#include "ws/Hub.h"

using namespace drogon;

namespace route {

// inDrive/Bykea-style bidding: the rider names a fare (anchored to the engine
// quote and bounded), nearby drivers bid, the rider accepts one -> that mints a ride.
class BiddingController : public HttpController<BiddingController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(BiddingController::offer, "/api/rides/offer", Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(BiddingController::bids, "/api/rides/offer/{1}/bids", Get, "route::JwtAuthFilter");
    ADD_METHOD_TO(BiddingController::accept, "/api/rides/offer/{1}/accept", Post, "route::JwtAuthFilter");
    METHOD_LIST_END

    Task<> offer(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
    Task<> bids(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
    Task<> accept(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, int id);
};

namespace {
std::string driverLabel(const orm::Row &d) {
    return d["name"].as<std::string>() + " \xC2\xB7 " + d["vehicle"].as<std::string>() +
           " \xC2\xB7 " + d["plate"].as<std::string>() + " \xC2\xB7 " + d["rating"].as<std::string>();
}
void pushStatus(long uid, int rideId, const std::string &status) {
    Json::Value ev; ev["type"] = "ride.status"; ev["ride_id"] = rideId; ev["status"] = status;
    Json::StreamWriterBuilder w; w["indentation"] = "";
    Hub::instance().publish("user:" + std::to_string(uid), Json::writeString(w, ev));
}
}  // namespace

Task<> BiddingController::offer(HttpRequestPtr req,
                               std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    if (!body || !(*body).isMember("origin_lat") || !(*body).isMember("dest_lat") ||
        !(*body).isMember("offer_fare")) {
        callback(jsonErr("Provide origin/dest coords and offer_fare"));
        co_return;
    }
    std::string mode = (*body).get("mode", "Taxi").asString();
    std::string origin = (*body).get("origin", "Pickup").asString();
    std::string dest = (*body).get("dest", "Destination").asString();
    double oLat = (*body)["origin_lat"].asDouble(), oLng = (*body)["origin_lng"].asDouble();
    double dLat = (*body)["dest_lat"].asDouble(), dLng = (*body)["dest_lng"].asDouble();

    double meters = fare::haversineMeters(oLat, oLng, dLat, dLng) * 1.3;
    double secs = meters / (28.0 * 1000.0 / 3600.0);
    double anchor = fare::compute(mode, meters, secs, Surge::instance().current(), false).total;

    double offerFare = (*body)["offer_fare"].asDouble();
    double lo = anchor * 0.6, hi = anchor * 1.4;
    if (offerFare < lo) offerFare = lo;
    if (offerFare > hi) offerFare = hi;
    offerFare = fare::roundTo(offerFare, 5);

    auto db = app().getDbClient();
    try {
        auto o = co_await db->execSqlCoro(
            "INSERT INTO ride_offers (user_id, mode, origin, dest, origin_lat, origin_lng, "
            "dest_lat, dest_lng, engine_anchor, offer_fare) "
            "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10) RETURNING id",
            uid, mode, origin, dest, oLat, oLng, dLat, dLng, anchor, offerFare);
        int offerId = o[0]["id"].as<int>();

        // Nearby drivers bid: one takes the offer, one counters higher, one at the engine price.
        auto ds = co_await db->execSqlCoro(
            "SELECT id,name,vehicle,plate,rating,lat,lng FROM drivers WHERE status='online' "
            "ORDER BY (lat-$1)*(lat-$1)+(lng-$2)*(lng-$2) LIMIT 3", oLat, oLng);
        Json::Value bidsArr(Json::arrayValue);
        int i = 0;
        for (const auto &d : ds) {
            double bid = (i == 0) ? offerFare : (i == 1) ? fare::roundTo(offerFare * 1.1, 5) : anchor;
            double dkm = fare::haversineMeters(oLat, oLng, d["lat"].as<double>(), d["lng"].as<double>()) / 1000.0;
            int eta = static_cast<int>(dkm * 3.0 + 2.0);
            if (eta < 2) eta = 2;
            if (eta > 20) eta = 20;
            auto bb = co_await db->execSqlCoro(
                "INSERT INTO ride_bids (offer_id, driver_id, bid_fare, eta_min) "
                "VALUES ($1,$2,$3,$4) RETURNING id",
                offerId, d["id"].as<int>(), bid, eta);
            Json::Value bj;
            bj["bid_id"] = bb[0]["id"].as<int>();
            bj["driver_id"] = d["id"].as<int>();
            bj["driver_label"] = driverLabel(d);
            bj["rating"] = d["rating"].as<double>();
            bj["bid_fare"] = bid;
            bj["eta_min"] = eta;
            bidsArr.append(bj);
            i++;
        }
        Json::Value data;
        data["offer_id"] = offerId;
        data["engine_anchor"] = anchor;
        data["offer_fare"] = offerFare;
        data["currency"] = "PKR";
        data["symbol"] = "Rs";
        data["bids"] = bidsArr;
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "offer failed: " << e.what();
        callback(jsonErr("Could not create offer", k500InternalServerError));
    }
    co_return;
}

Task<> BiddingController::bids(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback, int id) {
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT b.id, b.bid_fare, b.eta_min, b.status, d.id AS did, d.name, d.vehicle, "
            "d.plate, d.rating FROM ride_bids b JOIN drivers d ON d.id=b.driver_id "
            "WHERE b.offer_id=$1 ORDER BY b.bid_fare",
            id);
        Json::Value arr(Json::arrayValue);
        for (const auto &b : res) {
            Json::Value bj;
            bj["bid_id"] = b["id"].as<int>();
            bj["driver_id"] = b["did"].as<int>();
            bj["driver_label"] = b["name"].as<std::string>() + " \xC2\xB7 " +
                                 b["vehicle"].as<std::string>() + " \xC2\xB7 " +
                                 b["plate"].as<std::string>() + " \xC2\xB7 " + b["rating"].as<std::string>();
            bj["bid_fare"] = b["bid_fare"].as<double>();
            bj["eta_min"] = b["eta_min"].as<int>();
            bj["status"] = b["status"].as<std::string>();
            arr.append(bj);
        }
        callback(jsonOk(arr));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load bids", k500InternalServerError));
    }
    co_return;
}

Task<> BiddingController::accept(HttpRequestPtr req,
                                std::function<void(const HttpResponsePtr &)> callback, int id) {
    long uid = req->attributes()->get<long>("uid");
    auto body = req->getJsonObject();
    if (!body || !(*body).isMember("bid_id")) { callback(jsonErr("Provide bid_id")); co_return; }
    int bidId = (*body)["bid_id"].asInt();
    auto db = app().getDbClient();
    try {
        auto o = co_await db->execSqlCoro(
            "SELECT mode, origin, dest, origin_lat, origin_lng, dest_lat, dest_lng, status "
            "FROM ride_offers WHERE id=$1 AND user_id=$2", id, uid);
        if (o.size() == 0 || o[0]["status"].as<std::string>() != "open") {
            callback(jsonErr("Offer not found or already used", k409Conflict));
            co_return;
        }
        auto b = co_await db->execSqlCoro(
            "SELECT b.bid_fare, d.id AS did, d.name, d.vehicle, d.plate, d.rating, d.lat, d.lng "
            "FROM ride_bids b JOIN drivers d ON d.id=b.driver_id WHERE b.id=$1 AND b.offer_id=$2",
            bidId, id);
        if (b.size() == 0) { callback(jsonErr("Bid not found", k404NotFound)); co_return; }

        int driverId = b[0]["did"].as<int>();
        double farev = b[0]["bid_fare"].as<double>();
        std::string driver = b[0]["name"].as<std::string>() + " \xC2\xB7 " +
                             b[0]["vehicle"].as<std::string>() + " \xC2\xB7 " +
                             b[0]["plate"].as<std::string>() + " \xC2\xB7 " + b[0]["rating"].as<std::string>();
        double dsLat = b[0]["lat"].as<double>(), dsLng = b[0]["lng"].as<double>();
        std::string mode = o[0]["mode"].as<std::string>();
        double oLat = o[0]["origin_lat"].as<double>(), oLng = o[0]["origin_lng"].as<double>();
        double dLat = o[0]["dest_lat"].as<double>(), dLng = o[0]["dest_lng"].as<double>();

        co_await db->execSqlCoro("UPDATE ride_offers SET status='accepted' WHERE id=$1", id);
        co_await db->execSqlCoro("UPDATE ride_bids SET status='accepted' WHERE id=$1", bidId);
        co_await db->execSqlCoro("UPDATE drivers SET status='on_trip' WHERE id=$1", driverId);

        auto r = co_await db->execSqlCoro(
            "INSERT INTO ride_requests (user_id, origin, dest, mode, status, driver_label, "
            "fare_estimate, origin_lat, origin_lng, dest_lat, dest_lng, driver_start_lat, "
            "driver_start_lng, driver_id) "
            "VALUES ($1,$2,$3,$4,'assigned',$5,$6,$7,$8,$9,$10,$11,$12,$13) RETURNING id",
            uid, o[0]["origin"].as<std::string>(), o[0]["dest"].as<std::string>(), mode, driver,
            farev, oLat, oLng, dLat, dLng, dsLat, dsLng, driverId);
        int rideId = r[0]["id"].as<int>();

        auto *loop = app().getLoop();
        loop->runAfter(3.0, [uid, rideId] {
            app().getDbClient()->execSqlAsync("UPDATE ride_requests SET status='enroute' WHERE id=$1",
                [](const orm::Result &) {}, [](const orm::DrogonDbException &) {}, rideId);
            pushStatus(uid, rideId, "enroute");
        });
        loop->runAfter(8.0, [uid, rideId] {
            app().getDbClient()->execSqlAsync("UPDATE ride_requests SET status='arrived' WHERE id=$1",
                [](const orm::Result &) {}, [](const orm::DrogonDbException &) {}, rideId);
            pushStatus(uid, rideId, "arrived");
        });

        Json::Value data;
        data["ride_id"] = rideId;
        data["mode"] = mode;
        data["driver_label"] = driver;
        data["driver_id"] = driverId;
        data["fare_estimate"] = farev;
        data["status"] = "assigned";
        Json::Value dst; dst["lat"] = dsLat; dst["lng"] = dsLng; data["driver_start"] = dst;
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "accept bid failed: " << e.what();
        callback(jsonErr("Could not accept bid", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
