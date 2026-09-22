#pragma once
#include <optional>
#include <string>

namespace route::jwtutil {

struct Claims {
    long userId;
    std::string role;
};

std::string sign(long userId, const std::string &role);
std::optional<Claims> verify(const std::string &token);

}  // namespace route::jwtutil
