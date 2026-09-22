#include "auth/Jwt.h"

#include <drogon/drogon.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include <chrono>

namespace route::jwtutil {

using traits = jwt::traits::open_source_parsers_jsoncpp;

namespace {
std::string secret() {
    const auto &cfg = drogon::app().getCustomConfig();
    return cfg.get("jwt_secret", "insecure-dev-secret").asString();
}
constexpr const char *kIssuer = "route";
}  // namespace

std::string sign(long userId, const std::string &role) {
    auto now = std::chrono::system_clock::now();
    return jwt::create<traits>()
        .set_issuer(kIssuer)
        .set_type("JWT")
        .set_subject(std::to_string(userId))
        .set_payload_claim("role", jwt::basic_claim<traits>(role))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours{24})
        .sign(jwt::algorithm::hs256{secret()});
}

std::optional<Claims> verify(const std::string &token) {
    try {
        auto decoded = jwt::decode<traits>(token);
        jwt::verify<traits>()
            .allow_algorithm(jwt::algorithm::hs256{secret()})
            .with_issuer(kIssuer)
            .verify(decoded);
        Claims c;
        c.userId = std::stol(decoded.get_subject());
        c.role = decoded.get_payload_claim("role").as_string();
        return c;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace route::jwtutil
