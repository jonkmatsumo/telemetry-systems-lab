#pragma once

#include <chrono>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "obs/logging.h"

namespace telemetry {
namespace obs {

class HttpRequestLogScope {
public:
    HttpRequestLogScope(const httplib::Request& req,
                        httplib::Response& res,
                        const std::string& component,
                        const std::string& request_id,
                        nlohmann::json fields = nlohmann::json::object())
        : res_(&res),
          component_(component),
          request_id_(request_id),
          start_(std::chrono::steady_clock::now()) {
        fields_["route"] = req.path;
        fields_["method"] = req.method;
        if (!request_id.empty()) {
            fields_["request_id"] = request_id;
        }
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            fields_[it.key()] = it.value();
        }
        LogEvent(LogLevel::Info, "http_request_start", component_, fields_);
    }

    void AddFields(const nlohmann::json& extra) {
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            fields_[it.key()] = it.value();
        }
    }

    void RecordError(const std::string& error_code, const std::string& message, int status_code) {
        if (error_logged_) return;
        auto duration_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count();
        nlohmann::json payload = fields_;
        payload["status_code"] = status_code;
        payload["duration_ms"] = duration_ms;
        payload["error_code"] = error_code;
        payload["error"] = message;
        LogEvent(LogLevel::Error, "http_request_error", component_, payload);
        error_logged_ = true;
    }

    ~HttpRequestLogScope() {
        if (error_logged_) return;
        auto duration_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count();
        nlohmann::json payload = fields_;
        if (res_) {
            payload["status_code"] = res_->status;
        }
        payload["duration_ms"] = duration_ms;
        LogEvent(LogLevel::Info, "http_request_end", component_, payload);
    }

private:
    httplib::Response* res_;
    std::string component_;
    std::string request_id_;
    nlohmann::json fields_ = nlohmann::json::object();
    std::chrono::steady_clock::time_point start_;
    bool error_logged_ = false;
};

} // namespace obs
} // namespace telemetry
