#pragma once
#include <drogon/HttpController.h>

namespace route {

class TripController : public drogon::HttpController<TripController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TripController::list, "/api/trips", drogon::Get);
    ADD_METHOD_TO(TripController::seats, "/api/trips/{1}/seats", drogon::Get);
    METHOD_LIST_END

    drogon::Task<> list(drogon::HttpRequestPtr req,
                        std::function<void(const drogon::HttpResponsePtr &)> callback);
    drogon::Task<> seats(drogon::HttpRequestPtr req,
                         std::function<void(const drogon::HttpResponsePtr &)> callback,
                         int tripId);
};

}  // namespace route
