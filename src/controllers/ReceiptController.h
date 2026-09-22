#pragma once
#include <drogon/HttpController.h>

namespace route {

class ReceiptController : public drogon::HttpController<ReceiptController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ReceiptController::get, "/api/receipts/{1}", drogon::Get);
    ADD_METHOD_TO(ReceiptController::print, "/api/receipts/{1}/print", drogon::Get);
    METHOD_LIST_END

    drogon::Task<> get(drogon::HttpRequestPtr req,
                       std::function<void(const drogon::HttpResponsePtr &)> callback,
                       std::string receiptNo);
    drogon::Task<> print(drogon::HttpRequestPtr req,
                         std::function<void(const drogon::HttpResponsePtr &)> callback,
                         std::string receiptNo);
};

}  // namespace route
