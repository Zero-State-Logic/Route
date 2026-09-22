#include <drogon/HttpController.h>
#include <drogon/drogon.h>

#include <string>

#include "core/ApiResponse.h"
#include "core/FareEngine.h"
#include "core/Surge.h"

using namespace drogon;

namespace route {

// Server-side fare quote: fetches the real road route (OSRM shortest path) for the
// given pickup/destination, then prices it via FareEngine (distance + time + fuel).
class FareController : public HttpController<FareController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FareController::quote, "/api/fare/quote", Post);
    METHOD_LIST_END

    Task<> quote(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback);
};

namespace {
std::string coord(double v) { return std::to_string(v); }  // 6 decimals ~ 0.1m
}  // namespace

Task<> FareController::quote(HttpRequestPtr req,
                            std::function<void(const HttpResponsePtr &)> callback) {
    auto body = req->getJsonObject();
    if (!body || !(*body).isMember("origin_lat") || !(*body).isMember("dest_lat")) {
        callback(jsonErr("Provide origin_lat/lng and dest_lat/lng"));
        co_return;
    }
    double oLat = (*body)["origin_lat"].asDouble(), oLng = (*body)["origin_lng"].asDouble();
    double dLat = (*body)["dest_lat"].asDouble(), dLng = (*body)["dest_lng"].asDouble();
    std::string mode = (*body).get("mode", "Taxi").asString();

    double meters = 0, seconds = 0;
    bool onRoad = false;

    // 1) Real shortest-path distance/time from OSRM (server-authoritative).
    try {
        auto client = HttpClient::newHttpClient("https://routing.openstreetmap.de");
        auto r = HttpRequest::newHttpRequest();
        r->setMethod(Get);
        r->setPath("/routed-car/route/v1/driving/" + coord(oLng) + "," + coord(oLat) +
                   ";" + coord(dLng) + "," + coord(dLat));
        r->setParameter("overview", "false");
        auto resp = co_await client->sendRequestCoro(r, 6.0);
        auto j = resp->getJsonObject();
        if (j && (*j)["code"].asString() == "Ok" && (*j)["routes"].isArray() &&
            (*j)["routes"].size() > 0) {
            meters = (*j)["routes"][0]["distance"].asDouble();
            seconds = (*j)["routes"][0]["duration"].asDouble();
            onRoad = true;
        }
    } catch (const std::exception &e) {
        LOG_WARN << "OSRM quote failed, using haversine: " << e.what();
    }

    // 2) Fallback: straight-line * road factor, ~28 km/h urban.
    if (!onRoad || meters <= 0) {
        meters = fare::haversineMeters(oLat, oLng, dLat, dLng) * 1.3;
        seconds = meters / (28.0 * 1000.0 / 3600.0);
    }

    auto b = fare::compute(mode, meters, seconds, Surge::instance().current(), onRoad);
    Json::Value d;
    d["currency"] = b.currency;
    d["symbol"] = b.symbol;
    d["mode"] = mode;
    d["distance_km"] = b.distanceKm;
    d["duration_min"] = b.durationMin;
    d["base"] = b.base;
    d["distance_cost"] = b.distanceCost;
    d["time_cost"] = b.timeCost;
    d["fuel_litres"] = b.fuelLitres;
    d["fuel_cost"] = b.fuelCost;
    d["booking_fee"] = b.bookingFee;
    d["surge_mult"] = b.surgeMult;
    d["surge_amount"] = b.surgeAmount;
    d["total"] = b.total;
    d["on_road"] = b.onRoad;
    callback(jsonOk(d));
    co_return;
}

}  // namespace route
