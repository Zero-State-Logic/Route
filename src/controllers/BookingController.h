#pragma once
#include <drogon/HttpController.h>

namespace route {

class BookingController : public drogon::HttpController<BookingController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(BookingController::create, "/api/bookings", drogon::Post, "route::JwtAuthFilter");
    ADD_METHOD_TO(BookingController::list, "/api/bookings", drogon::Get, "route::JwtAuthFilter");
    METHOD_LIST_END

    drogon::Task<> create(drogon::HttpRequestPtr req,
                          std::function<void(const drogon::HttpResponsePtr &)> callback);
    drogon::Task<> list(drogon::HttpRequestPtr req,
                        std::function<void(const drogon::HttpResponsePtr &)> callback);
};

}  // namespace route
