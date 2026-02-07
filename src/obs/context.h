#pragma once

#include <string>

namespace telemetry {
namespace obs {

struct Context {
    std::string request_id;
    std::string trace_id;
    std::string user_id;
    std::string dataset_id;
    std::string model_run_id;
    std::string inference_run_id;
    std::string score_job_id;
};

inline thread_local Context g_context{};
inline thread_local bool g_context_set = false;

inline const Context& GetContext() {
    return g_context;
}

inline bool HasContext() {
    return g_context_set;
}

inline void SetContext(const Context& ctx) {
    g_context = ctx;
    g_context_set = true;
}

inline void ClearContext() {
    g_context = Context{};
    g_context_set = false;
}

inline void UpdateContext(const Context& ctx) {
    g_context = ctx;
    g_context_set = true;
}

class ScopedContext {
public:
    explicit ScopedContext(const Context& ctx)
        : prev_(g_context), prev_set_(g_context_set) {
        SetContext(ctx);
    }

    ~ScopedContext() {
        if (prev_set_) {
            g_context = prev_;
            g_context_set = true;
        } else {
            ClearContext();
        }
    }

private:
    Context prev_{};
    bool prev_set_ = false;
};

} // namespace obs
} // namespace telemetry
