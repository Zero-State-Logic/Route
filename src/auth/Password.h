#pragma once
#include <string>

// Salted PBKDF2-HMAC-SHA256 password hashing (OpenSSL, shipped with Drogon).
namespace route::pwd {
std::string hash(const std::string &plain);
bool verify(const std::string &plain, const std::string &stored);
}  // namespace route::pwd
