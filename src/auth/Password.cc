#include "auth/Password.h"

#include <drogon/utils/Utilities.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <sstream>
#include <vector>

namespace route::pwd {

namespace {
constexpr int kIterations = 120000;
constexpr int kSaltLen = 16;
constexpr int kKeyLen = 32;

std::string b64(const unsigned char *data, size_t len) {
    return drogon::utils::base64Encode(data, len);
}

std::vector<unsigned char> pbkdf2(const std::string &plain,
                                  const unsigned char *salt, size_t saltLen,
                                  int iterations) {
    std::vector<unsigned char> out(kKeyLen);
    PKCS5_PBKDF2_HMAC(plain.c_str(), static_cast<int>(plain.size()), salt,
                      static_cast<int>(saltLen), iterations, EVP_sha256(),
                      kKeyLen, out.data());
    return out;
}
}  // namespace

std::string hash(const std::string &plain) {
    unsigned char salt[kSaltLen];
    RAND_bytes(salt, kSaltLen);
    auto dk = pbkdf2(plain, salt, kSaltLen, kIterations);
    std::ostringstream os;
    os << "pbkdf2$" << kIterations << "$" << b64(salt, kSaltLen) << "$"
       << b64(dk.data(), dk.size());
    return os.str();
}

bool verify(const std::string &plain, const std::string &stored) {
    // Format: pbkdf2$<iter>$<b64salt>$<b64hash>
    std::vector<std::string> parts;
    std::stringstream ss(stored);
    std::string item;
    while (std::getline(ss, item, '$')) parts.push_back(item);
    if (parts.size() != 4 || parts[0] != "pbkdf2") return false;

    int iterations = 0;
    try {
        iterations = std::stoi(parts[1]);
    } catch (...) {
        return false;
    }
    std::vector<char> salt = drogon::utils::base64DecodeToVector(parts[2]);
    std::vector<char> expected = drogon::utils::base64DecodeToVector(parts[3]);
    auto dk = pbkdf2(plain, reinterpret_cast<const unsigned char *>(salt.data()),
                     salt.size(), iterations);
    if (dk.size() != expected.size()) return false;

    unsigned char diff = 0;  // constant-time compare
    for (size_t i = 0; i < dk.size(); ++i)
        diff |= static_cast<unsigned char>(dk[i] ^ static_cast<unsigned char>(expected[i]));
    return diff == 0;
}

}  // namespace route::pwd
