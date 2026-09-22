#pragma once
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <string>

namespace route {

inline drogon::HttpResponsePtr jsonOk(Json::Value data) {
    Json::Value env;
    env["success"] = true;
    env["data"] = std::move(data);
    env["error"] = Json::nullValue;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(env);
    resp->setStatusCode(drogon::k200OK);
    return resp;
}

inline drogon::HttpResponsePtr jsonErr(const std::string &message,
                                       drogon::HttpStatusCode code = drogon::k400BadRequest) {
    Json::Value env;
    env["success"] = false;
    env["data"] = Json::nullValue;
    env["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(env);
    resp->setStatusCode(code);
    return resp;
}

}  // namespace route
