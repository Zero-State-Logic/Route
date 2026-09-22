#include <drogon/drogon.h>

#include "core/Surge.h"
#include "ws/Hub.h"

using namespace drogon;

int main() {
    app().loadConfigFile("config.json");
    LOG_INFO << "Route server starting on http://localhost:8080";

    // Periodically push live operations metrics to any subscribed manager clients.
    app().getLoop()->runEvery(3.0, [] {
        auto db = app().getDbClient();
        db->execSqlAsync(
            "SELECT count(*) FILTER (WHERE status='booked') AS booked, count(*) AS total "
            "FROM trip_seats",
            [](const orm::Result &r) {
                long booked = r[0]["booked"].as<long>();
                long total = r[0]["total"].as<long>();
                Json::Value ev;
                ev["type"] = "ops.tick";
                ev["occupancy_pct"] =
                    total > 0 ? static_cast<int>(100.0 * booked / total) : 0;
                Json::StreamWriterBuilder w;
                w["indentation"] = "";
                route::Hub::instance().publish("ops:dashboard",
                                               Json::writeString(w, ev));
            },
            [](const orm::DrogonDbException &) {});
    });

    // Release seat holds whose TTL has expired, so abandoned checkouts free up
    // inventory. (Held seats are created by a future two-phase checkout flow.)
    app().getLoop()->runEvery(15.0, [] {
        app().getDbClient()->execSqlAsync(
            "UPDATE trip_seats SET status='free', hold_expires_at=NULL "
            "WHERE status='held' AND hold_expires_at < now()",
            [](const orm::Result &) {}, [](const orm::DrogonDbException &) {});
    });

    // Demand-based surge: recompute every 20s from recent open ride requests,
    // rate-limited, and stream it to riders (pricing) + managers (ops:dashboard).
    app().getLoop()->runEvery(20.0, [] {
        app().getDbClient()->execSqlAsync(
            "SELECT count(*) AS demand FROM ride_requests "
            "WHERE created_at > now() - interval '15 minutes' "
            "AND status NOT IN ('completed','cancelled')",
            [](const orm::Result &r) {
                long demand = r[0]["demand"].as<long>();
                double target = 1.0 + 0.15 * static_cast<double>(demand > 2 ? demand - 2 : 0);
                double cur = route::Surge::instance().current();
                double step = target - cur;
                if (step > 0.1) step = 0.1;
                else if (step < -0.1) step = -0.1;
                route::Surge::instance().setAuto(cur + step);
                Json::Value ev;
                ev["type"] = "surge.update";
                ev["multiplier"] = route::Surge::instance().current();
                ev["manual"] = route::Surge::instance().isManual();
                Json::StreamWriterBuilder w; w["indentation"] = "";
                std::string msg = Json::writeString(w, ev);
                route::Hub::instance().publish("pricing", msg);
                route::Hub::instance().publish("ops:dashboard", msg);
            },
            [](const orm::DrogonDbException &) {});
    });

    // Simulate live driver GPS: nudge active drivers and stream positions to ops:map.
    app().getLoop()->runEvery(4.0, [] {
        app().getDbClient()->execSqlAsync(
            "UPDATE drivers SET lat = lat + (random()-0.5)*0.0012, "
            "lng = lng + (random()-0.5)*0.0012, last_seen = now() "
            "WHERE status IN ('online','on_trip')",
            [](const orm::Result &) {
                app().getDbClient()->execSqlAsync(
                    "SELECT id,name,status,lat,lng FROM drivers WHERE lat IS NOT NULL",
                    [](const orm::Result &r) {
                        Json::Value units(Json::arrayValue);
                        for (const auto &row : r) {
                            Json::Value u;
                            u["id"] = row["id"].as<int>();
                            u["name"] = row["name"].as<std::string>();
                            u["status"] = row["status"].as<std::string>();
                            u["lat"] = row["lat"].as<double>();
                            u["lng"] = row["lng"].as<double>();
                            units.append(u);
                        }
                        Json::Value ev;
                        ev["type"] = "unit.location";
                        ev["units"] = units;
                        Json::StreamWriterBuilder w; w["indentation"] = "";
                        route::Hub::instance().publish("ops:map", Json::writeString(w, ev));
                    },
                    [](const orm::DrogonDbException &) {});
            },
            [](const orm::DrogonDbException &) {});
    });

    app().run();
    return 0;
}
