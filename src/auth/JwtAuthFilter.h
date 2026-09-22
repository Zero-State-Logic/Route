#pragma once
#include <drogon/HttpFilter.h>

namespace route {

// Rejects requests without a valid Bearer token. On success it stores the
// authenticated user id ("uid", long) and "role" (string) in request attributes.
class JwtAuthFilter : public drogon::HttpFilter<JwtAuthFilter> {
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};

}  // namespace route
