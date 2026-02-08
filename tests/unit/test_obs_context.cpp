#include <gtest/gtest.h>
#include "obs/context.h"
#include "obs/logging.h"
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>

namespace {

using namespace telemetry::obs;

TEST(ObsContextTest, LogEventIncludesContext) {
    // Setup a custom logger to capture output
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto logger = std::make_shared<spdlog::logger>("test_context", sink);
    logger->set_pattern("%v"); // Only the message
    auto old_default = spdlog::default_logger();
    spdlog::set_default_logger(logger);

    {
        Context ctx;
        ctx.request_id = "req-123";
        ctx.user_id = "user-456";
        ScopedContext scope(ctx);

        LogEvent(LogLevel::Info, "test_event", "test_component", {{"extra", "val"}});
    }

    spdlog::set_default_logger(old_default);

    std::string output = oss.str();
    nlohmann::json j = nlohmann::json::parse(output);

    EXPECT_EQ(j["event"], "test_event");
    EXPECT_EQ(j["request_id"], "req-123");
    EXPECT_EQ(j["user_id"], "user-456");
    EXPECT_EQ(j["extra"], "val");
}

TEST(ObsContextTest, ScopedContextNesting) {
    Context outer;
    outer.request_id = "outer";
    
    {
        ScopedContext s1(outer);
        EXPECT_EQ(GetContext().request_id, "outer");
        
        Context inner;
        inner.request_id = "inner";
        {
            ScopedContext s2(inner);
            EXPECT_EQ(GetContext().request_id, "inner");
        }
        
        EXPECT_EQ(GetContext().request_id, "outer");
    }
    
    EXPECT_FALSE(HasContext());
}

} // namespace
