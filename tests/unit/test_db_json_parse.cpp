#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "db_json_parse.h"
#include "metrics.h"

namespace {

auto ReadCounterValue(const std::string& metric_key) -> long {
    const auto text = telemetry::metrics::MetricsRegistry::Instance().ToPrometheus();
    std::istringstream stream(text);
    std::string key;
    long value = 0;

    while (stream >> key >> value) {
        if (key == metric_key) {
            return value;
        }
    }
    return 0;
}

TEST(DbJsonParseTest, InvalidJsonIncrementsMetricAndLogsWarning) {
    std::ostringstream log_capture;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log_capture);
    auto logger = std::make_shared<spdlog::logger>("db_json_parse_test_logger", sink);
    logger->set_level(spdlog::level::warn);

    auto previous_logger = spdlog::default_logger();
    auto previous_level = spdlog::get_level();
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::warn);

    const std::string metric_key =
        "db_json_parse_failures_total{field=\"training_config\",operation=\"GetModelRun\"}";
    const long before = ReadCounterValue(metric_key);

    auto parsed = telemetry::db::ParseJsonPayloadWithObservability(
        "{not-valid-json",
        "training_config",
        "model-run-123",
        "GetModelRun",
        nlohmann::json::object());

    spdlog::set_default_logger(previous_logger);
    spdlog::set_level(previous_level);

    EXPECT_TRUE(parsed.is_object());
    EXPECT_EQ(ReadCounterValue(metric_key), before + 1);

    const std::string logs = log_capture.str();
    EXPECT_NE(logs.find("training_config"), std::string::npos);
    EXPECT_NE(logs.find("model-run-123"), std::string::npos);
}

TEST(DbJsonParseTest, ValidJsonDoesNotIncrementFailureMetric) {
    const std::string metric_key =
        "db_json_parse_failures_total{field=\"details\",operation=\"GetInferenceRun\"}";
    const long before = ReadCounterValue(metric_key);

    auto parsed = telemetry::db::ParseJsonPayloadWithObservability(
        R"({"ok":true,"value":7})",
        "details",
        "inference-123",
        "GetInferenceRun",
        nlohmann::json::array());

    ASSERT_TRUE(parsed.is_object());
    EXPECT_TRUE(parsed["ok"].get<bool>());
    EXPECT_EQ(parsed["value"].get<int>(), 7);
    EXPECT_EQ(ReadCounterValue(metric_key), before);
}

} // namespace
