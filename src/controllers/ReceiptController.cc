#include "controllers/ReceiptController.h"

#include <drogon/drogon.h>

#include "core/ApiResponse.h"

using namespace drogon;

namespace route {

Task<> ReceiptController::get(HttpRequestPtr req,
                            std::function<void(const HttpResponsePtr &)> callback,
                            std::string receiptNo) {
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT receipt_no, amount, issued_at, payload FROM receipts "
            "WHERE receipt_no=$1",
            receiptNo);
        if (res.size() == 0) {
            callback(jsonErr("Receipt not found", k404NotFound));
            co_return;
        }
        Json::Value data;
        data["receipt_no"] = res[0]["receipt_no"].as<std::string>();
        data["amount"] = res[0]["amount"].as<double>();
        data["issued_at"] = res[0]["issued_at"].as<std::string>();
        Json::Value payload;
        Json::CharReaderBuilder b;
        std::string errs;
        std::string raw = res[0]["payload"].as<std::string>();
        std::unique_ptr<Json::CharReader> reader(b.newCharReader());
        reader->parse(raw.c_str(), raw.c_str() + raw.size(), &payload, &errs);
        data["detail"] = payload;
        callback(jsonOk(data));
    } catch (const std::exception &e) {
        callback(jsonErr("Could not load receipt", k500InternalServerError));
    }
    co_return;
}

Task<> ReceiptController::print(HttpRequestPtr req,
                              std::function<void(const HttpResponsePtr &)> callback,
                              std::string receiptNo) {
    auto db = app().getDbClient();
    try {
        auto res = co_await db->execSqlCoro(
            "SELECT receipt_no, amount, issued_at, payload FROM receipts "
            "WHERE receipt_no=$1",
            receiptNo);
        if (res.size() == 0) {
            auto r = HttpResponse::newHttpResponse();
            r->setStatusCode(k404NotFound);
            r->setBody("Receipt not found");
            callback(r);
            co_return;
        }
        std::string no = res[0]["receipt_no"].as<std::string>();
        std::string amount = res[0]["amount"].as<std::string>();
        std::string issued = res[0]["issued_at"].as<std::string>();
        std::string html =
            "<!doctype html><html><head><meta charset='utf-8'>"
            "<title>Receipt " + no + "</title>"
            "<style>body{font-family:system-ui;background:#f6f1e7;color:#273e47;"
            "padding:40px}.r{max-width:420px;margin:auto;background:#fff;border-radius:14px;"
            "overflow:hidden;border:1px solid #e7dfcd}.h{background:#d8973c;color:#2c1d06;"
            "padding:18px 22px;font-weight:700;font-size:20px}.b{padding:22px}"
            ".row{display:flex;justify-content:space-between;padding:8px 0;"
            "border-bottom:1px dashed #e7dfcd}</style></head><body>"
            "<div class='r'><div class='h'>Route - payment received</div><div class='b'>"
            "<div class='row'><span>Receipt no.</span><b>" + no + "</b></div>"
            "<div class='row'><span>Issued</span><span>" + issued + "</span></div>"
            "<div class='row'><span>Total paid</span><b>$" + amount + "</b></div>"
            "<p style='margin-top:18px;color:#5d6f79'>Thank you for riding with Route. "
            "Use your browser Print to save this as a PDF.</p>"
            "</div></div></body></html>";
        auto r = HttpResponse::newHttpResponse();
        r->setContentTypeCode(CT_TEXT_HTML);
        r->setBody(html);
        callback(r);
    } catch (const std::exception &e) {
        callback(jsonErr("Could not render receipt", k500InternalServerError));
    }
    co_return;
}

}  // namespace route
