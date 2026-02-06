#include <gtest/gtest.h>
#include "generator.h"
#include <chrono>
#include <string>

// Forward declaration if not in header, but it seems it is a helper in cpp.
// Check generator.h first. If not exposed, I might need to make it exposed or test via Generator.
// Let's check generator.h first.

TEST(TimeParsingTest, ParseTimeRespectsZulu) {
    // 2024-01-01T00:00:00Z is 1704067200 unix timestamp
    std::string iso = "2024-01-01T00:00:00Z";
    auto tp = ParseTime(iso);
    auto epoch = std::chrono::system_clock::to_time_t(tp);
    
    EXPECT_EQ(epoch, 1704067200);
}

TEST(TimeParsingTest, ParseTimeEdges) {
    // Leap year Feb 29
    std::string iso = "2024-02-29T12:00:00Z";
    // 1709208000
    auto tp = ParseTime(iso);
    auto epoch = std::chrono::system_clock::to_time_t(tp);
    EXPECT_EQ(epoch, 1709208000);
}
