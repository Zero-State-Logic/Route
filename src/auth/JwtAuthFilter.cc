#include "auth/JwtAuthFilter.h"

#include "auth/Jwt.h"
#include "core/ApiResponse.h"

using namespace drogon;

namespace route {

void JwtAuthFilter::doFilter(const HttpRequestPtr &req, FilterCallback &&fcb,
                             FilterChainCallback &&fccb) {
    std::string header = req->getHeader("authorization");
    const std::string prefix = "Bearer ";
    if (header.rfind(prefix, 0) != 0) {
        fcb(jsonErr("Missing or malformed Authorization header", k401Unauthorized));
        return;
    }
    auto claims = jwtutil::verify(header.substr(prefix.size()));
    if (!claims) {
        fcb(jsonErr("Invalid or expired token", k401Unauthorized));
        return;
    }
    req->attributes()->insert("uid", claims->userId);
    req->attributes()->insert("role", claims->role);
    fccb();
}

}  // namespace route
