#pragma once
#include <drogon/HttpController.h>

namespace route {

class RentalController : public drogon::HttpController<RentalController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RentalController::vehicles, "/api/rentals/vehicles", drogon::Get);
    ADD_METHOD_TO(RentalController::start, "/api/rentals/start", drogon::Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(RentalController::stop, "/api/rentals/stop", drogon::Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(RentalController::active, "/api/rentals/active", drogon::Get, "route::JwtAuthFilter");
    METHOD_LIST_END

    drogon::Task<> vehicles(drogon::HttpRequestPtr req,
                            std::function<void(const drogon::HttpResponsePtr &)> callback);
    drogon::Task<> start(drogon::HttpRequestPtr req,
                         std::function<void(const drogon::HttpResponsePtr &)> callback);
    drogon::Task<> stop(drogon::HttpRequestPtr req,
                        std::function<void(const drogon::HttpResponsePtr &)> callback);
    drogon::Task<> active(drogon::HttpRequestPtr req,
                          std::function<void(const drogon::HttpResponsePtr &)> callback);
};

}  // namespace route
