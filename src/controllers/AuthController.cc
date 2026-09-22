#include "controllers/AuthController.h"

#include <drogon/drogon.h>

#include "auth/Jwt.h"
#include "auth/Password.h"
#include "core/ApiResponse.h"

using namespace drogon;

namespace route {

namespace {
Json::Value userJson(long id, const std::string &email, const std::string &name,
                     const std::string &role) {
    Json::Value u;
    u["id"] = static_cast<Json::Int64>(id);
    u["email"] = email;
    u["full_name"] = name;
    u["role"] = role;
    return u;
}
}  // namespace

Task<> AuthController::signup(HttpRequestPtr req,
                             std::function<void(const HttpResponsePtr &)> callback) {
    auto body = req->getJsonObject();
    if (!body) {
        callback(jsonErr("Expected a JSON body"));
        co_return;
    }
    const std::string email = (*body).get("email", "").asString();
    const std::string password = (*body).get("password", "").asString();
    const std::string fullName = (*body).get("full_name", "").asString();
    std::string role = (*body).get("role", "rider").asString();
    if (role != "rider" && role != "manager") role = "rider";

    if (email.empty() || password.size() < 6 || fullName.empty()) {
        callback(jsonErr("Email, full name and a 6+ char password are required"));
        co_return;
    }

    auto db = app().getDbClient();
    try {
        auto existing = co_await db->execSqlCoro(
            "SELECT id FROM users WHERE email=$1", email);
        if (existing.size() > 0) {
            callback(jsonErr("An account with that email already exists", k409Conflict));
            co_return;
        }
        auto res = co_await db->execSqlCoro(
            "INSERT INTO users (email, password_hash, full_name, role) "
            "VALUES ($1,$2,$3,$4) RETURNING id",
            email, pwd::hash(password), fullName, role);
        long id = res[0]["id"].as<long>();

        Json::Value data;
        data["token"] = jwtutil::sign(id, role);
        data["user"] = userJson(id, email, fullName, role);
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "signup failed: " << e.what();
        callback(jsonErr("Could not create account", k500InternalServerError));
    }
    co_return;
}

Task<> AuthController::login(HttpRequestPtr req,
                            std::function<void(const HttpResponsePtr &)> callback) {
    auto body = req->getJsonObject();
    if (!body) {
        callback(jsonErr("Expected a JSON body"));
        co_return;
    }
    const std::string email = (*body).get("email", "").asString();
    const std::string password = (*body).get("password", "").asString();

    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT id, password_hash, full_name, role FROM users WHERE email=$1", email);
        if (res.size() == 0 ||
            !pwd::verify(password, res[0]["password_hash"].as<std::string>())) {
            callback(jsonErr("Invalid email or password", k401Unauthorized));
            co_return;
        }
        long id = res[0]["id"].as<long>();
        std::string role = res[0]["role"].as<std::string>();
        Json::Value data;
        data["token"] = jwtutil::sign(id, role);
        data["user"] = userJson(id, email, res[0]["full_name"].as<std::string>(), role);
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        LOG_ERROR << "login failed: " << e.what();
        callback(jsonErr("Login failed", k500InternalServerError));
    }
    co_return;
}

Task<> AuthController::me(HttpRequestPtr req,
                         std::function<void(const HttpResponsePtr &)> callback) {
    long uid = req->attributes()->get<long>("uid");
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT id, email, full_name, role FROM users WHERE id=$1", uid);
        if (res.size() == 0) {
            callback(jsonErr("User not found", k404NotFound));
            co_return;
        }
        callback(jsonOk(userJson(res[0]["id"].as<long>(),
                                 res[0]["email"].as<std::string>(),
                                 res[0]["full_name"].as<std::string>(),
                                 res[0]["role"].as<std::string>())));
    } catch (const std::exception &e) {
        callback(jsonErr("Lookup failed", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
