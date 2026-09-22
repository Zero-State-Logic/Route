#pragma once
#include <drogon/HttpController.h>

namespace route {

class AuthController : public drogon::HttpController<AuthController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::signup, "/api/auth/signup", drogon::Post);
    ADD_METHOD_TO(AuthController::login, "/api/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::me, "/api/auth/me", drogon::Get, "route::JwtAuthFilter");
    METHOD_LIST_END

    drogon::Task<> signup(drogon::HttpRequestPtr req,
                          std::function<void(const drogon::HttpResponsePtr &)> callback);
    drogon::Task<> login(drogon::HttpRequestPtr req,
                         std::function<void(const drogon::HttpResponsePtr &)> callback);
    drogon::Task<> me(drogon::HttpRequestPtr req,
                      std::function<void(const drogon::HttpResponsePtr &)> callback);
};

}  // namespace route
