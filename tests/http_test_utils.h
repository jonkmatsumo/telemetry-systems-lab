#pragma once

#include <arpa/inet.h>
#include <chrono>
#include <httplib.h>
#include <netinet/in.h>
#include <atomic>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

inline int AllocateTestPort() {
    static std::atomic<int> fallback_port{55000};
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return fallback_port.fetch_add(1, std::memory_order_relaxed);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0); // Ask OS for any free port.
#if defined(__APPLE__)
    addr.sin_len = sizeof(addr);
#endif

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return fallback_port.fetch_add(1, std::memory_order_relaxed);
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(fd);
        return fallback_port.fetch_add(1, std::memory_order_relaxed);
    }

    int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

inline bool WaitForServerReady(const std::string& host, int port, int max_retries = 100, int sleep_ms = 50) {
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        try {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd >= 0) {
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(static_cast<uint16_t>(port));
#if defined(__APPLE__)
                addr.sin_len = sizeof(addr);
#endif
                if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1 &&
                    ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                    ::close(fd);
                    return true;
                }
                ::close(fd);
            }

            httplib::Client cli(host, port);
            if (auto res = cli.Get("/healthz")) {
                (void)res;
                return true;
            }
            if (auto res = cli.Get("/health")) {
                (void)res;
                return true;
            }
        } catch (...) {
            // Retry until server comes up.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    return false;
}
