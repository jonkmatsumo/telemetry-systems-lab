#include <iostream>
#include <spdlog/spdlog.h>

int main() {
    spdlog::info("Telemetry Generator Service Starting...");
    // Keep alive for Docker integration test
    while(true) {
        // busy wait loop or sleep
    }
    return 0;
}
