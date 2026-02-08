#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../metrics.h"
#include "obs/context.h"
#include "obs/logging.h"

namespace telemetry::obs {

inline auto EmitCounter(const std::string& name,
                        long value,
                        const std::string& unit,
                        const std::string& component,
                        const std::map<std::string, std::string>& labels = {},
                        const nlohmann::json& fields = nlohmann::json::object()) -> void {
    ::telemetry::metrics::MetricsRegistry::Instance().Increment(name, labels, value);
    nlohmann::json payload = fields;
    payload["metric_name"] = name;
    payload["value"] = value;
    payload["unit"] = unit;
    if (HasContext()) {
        const auto& ctx = GetContext();
        if (!ctx.request_id.empty() && !payload.contains("request_id")) { payload["request_id"] = ctx.request_id; }
        if (!ctx.dataset_id.empty() && !payload.contains("dataset_id")) { payload["dataset_id"] = ctx.dataset_id; }
        if (!ctx.model_run_id.empty() && !payload.contains("model_run_id")) { payload["model_run_id"] = ctx.model_run_id; }
        if (!ctx.inference_run_id.empty() && !payload.contains("inference_run_id")) { payload["inference_run_id"] = ctx.inference_run_id; }
        if (!ctx.score_job_id.empty() && !payload.contains("score_job_id")) { payload["score_job_id"] = ctx.score_job_id; }
    }
    if (!labels.empty()) {
        nlohmann::json l = nlohmann::json::object();
        for (const auto& kv : labels) {
            l[kv.first] = kv.second;
        }
        payload["labels"] = l;
    }
    LogEvent(LogLevel::Info, "metric", component, payload);
}

inline auto EmitHistogram(const std::string& name,
                          double value,
                          const std::string& unit,
                          const std::string& component,
                          const std::map<std::string, std::string>& labels = {},
                          const nlohmann::json& fields = nlohmann::json::object()) -> void {
    ::telemetry::metrics::MetricsRegistry::Instance().RecordLatency(name, labels, value);
    nlohmann::json payload = fields;
    payload["metric_name"] = name;
    payload["value"] = value;
    payload["unit"] = unit;
    if (HasContext()) {
        const auto& ctx = GetContext();
        if (!ctx.request_id.empty() && !payload.contains("request_id")) { payload["request_id"] = ctx.request_id; }
        if (!ctx.dataset_id.empty() && !payload.contains("dataset_id")) { payload["dataset_id"] = ctx.dataset_id; }
        if (!ctx.model_run_id.empty() && !payload.contains("model_run_id")) { payload["model_run_id"] = ctx.model_run_id; }
        if (!ctx.inference_run_id.empty() && !payload.contains("inference_run_id")) { payload["inference_run_id"] = ctx.inference_run_id; }
        if (!ctx.score_job_id.empty() && !payload.contains("score_job_id")) { payload["score_job_id"] = ctx.score_job_id; }
    }
    if (!labels.empty()) {
        nlohmann::json l = nlohmann::json::object();
        for (const auto& kv : labels) {
            l[kv.first] = kv.second;
        }
        payload["labels"] = l;
    }
    LogEvent(LogLevel::Info, "metric", component, payload);
}

inline auto EmitGauge(const std::string& name,
                      double value,
                      const std::string& unit,
                      const std::string& component,
                      const std::map<std::string, std::string>& labels = {},
                      const nlohmann::json& fields = nlohmann::json::object()) -> void {
    ::telemetry::metrics::MetricsRegistry::Instance().SetGauge(name, value);
    nlohmann::json payload = fields;
    payload["metric_name"] = name;
    payload["value"] = value;
    payload["unit"] = unit;
    if (HasContext()) {
        const auto& ctx = GetContext();
        if (!ctx.request_id.empty() && !payload.contains("request_id")) { payload["request_id"] = ctx.request_id; }
        if (!ctx.dataset_id.empty() && !payload.contains("dataset_id")) { payload["dataset_id"] = ctx.dataset_id; }
        if (!ctx.model_run_id.empty() && !payload.contains("model_run_id")) { payload["model_run_id"] = ctx.model_run_id; }
        if (!ctx.inference_run_id.empty() && !payload.contains("inference_run_id")) { payload["inference_run_id"] = ctx.inference_run_id; }
        if (!ctx.score_job_id.empty() && !payload.contains("score_job_id")) { payload["score_job_id"] = ctx.score_job_id; }
    }
    if (!labels.empty()) {
        nlohmann::json l = nlohmann::json::object();
        for (const auto& kv : labels) {
            l[kv.first] = kv.second;
        }
        payload["labels"] = l;
    }
    LogEvent(LogLevel::Info, "metric", component, payload);
}

} // namespace telemetry::obs