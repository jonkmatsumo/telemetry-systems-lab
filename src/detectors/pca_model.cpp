#include "pca_model.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "obs/metrics.h"
#include "obs/error_codes.h"

namespace telemetry::anomaly {

using json = nlohmann::json;

static auto vec_sub(const linalg::Vector& a, const linalg::Vector& b) -> linalg::Vector {
    if (a.size() != b.size()) { throw std::runtime_error("vec_sub dimension mismatch"); }
    linalg::Vector out(a.size(), 0.0);
    for (size_t i = 0; i < a.size(); ++i) { out[i] = a[i] - b[i]; }
    return out;
}

static auto vec_add(const linalg::Vector& a, const linalg::Vector& b) -> linalg::Vector {
    if (a.size() != b.size()) { throw std::runtime_error("vec_add dimension mismatch"); }
    linalg::Vector out(a.size(), 0.0);
    for (size_t i = 0; i < a.size(); ++i) { out[i] = a[i] + b[i]; }
    return out;
}

static auto vec_div(const linalg::Vector& a, const linalg::Vector& b) -> linalg::Vector {
    if (a.size() != b.size()) { throw std::runtime_error("vec_div dimension mismatch"); }
    linalg::Vector out(a.size(), 0.0);
    for (size_t i = 0; i < a.size(); ++i) { out[i] = a[i] / b[i]; }
    return out;
}

auto PcaModel::Load(const std::string& artifact_path) -> void {
    auto start = std::chrono::steady_clock::now();
    nlohmann::json start_fields = {{"artifact_path", artifact_path}};
    if (telemetry::obs::HasContext()) {
        const auto& ctx = telemetry::obs::GetContext();
        if (!ctx.request_id.empty()) { start_fields["request_id"] = ctx.request_id; }
        if (!ctx.model_run_id.empty()) { start_fields["model_run_id"] = ctx.model_run_id; }
        if (!ctx.inference_run_id.empty()) { start_fields["inference_run_id"] = ctx.inference_run_id; }
    }
    telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "model_load_start", "model", start_fields);
    std::ifstream f(artifact_path);
    if (!f.is_open()) {
        nlohmann::json error_fields = start_fields;
        error_fields["error_code"] = telemetry::obs::kErrModelLoadFailed;
        error_fields["error"] = "Failed to open artifact";
        telemetry::obs::LogEvent(telemetry::obs::LogLevel::Error, "model_load_error", "model", error_fields);
        throw std::runtime_error("Failed to open artifact: " + artifact_path);
    }
    
    json j;
    f >> j;
    
    // 1. Preprocessing
    auto raw_mean = j["preprocessing"]["mean"].get<std::vector<double>>();
    auto raw_scale = j["preprocessing"]["scale"].get<std::vector<double>>();
    
    if (raw_mean.size() != FeatureVector::kSize || raw_scale.size() != FeatureVector::kSize) {
        throw std::runtime_error("Dimension mismatch in artifact preprocessing");
    }

    cur_mean_ = raw_mean;
    cur_scale_ = raw_scale;

    // 2. PCA
    // Components are stored as list of lists (k x d)
    auto raw_components = j["model"]["components"].get<std::vector<std::vector<double>>>();
    int k = static_cast<int>(raw_components.size());
    if (k == 0) { throw std::runtime_error("No PCA components found"); }
    int d = static_cast<int>(raw_components[0].size());
    if (d != FeatureVector::kSize) { throw std::runtime_error("Dimension mismatch in PCA components"); }

    // Copy to matrix
    components_ = linalg::Matrix(static_cast<size_t>(k), static_cast<size_t>(d));
    for (size_t i = 0; i < static_cast<size_t>(k); ++i) {
        for (size_t c = 0; c < static_cast<size_t>(d); ++c) {
            components_(i, c) = raw_components[i][c];
        }
    }

    auto raw_pca_mean = j["model"]["mean"].get<std::vector<double>>();
    pca_mean_ = raw_pca_mean;

    // 3. Thresholds
    threshold_ = j["thresholds"]["reconstruction_error"].get<double>();

    loaded_ = true;
    spdlog::info("PcaModel loaded from {}. Dimensions: {}x{}, Threshold: {}", 
        artifact_path, k, d, threshold_);

    auto end = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
    telemetry::obs::EmitHistogram("model_load_duration_ms", duration_ms, "ms", "model",
                                  {}, {{"artifact_path", artifact_path}});
    std::error_code ec;
    auto size = std::filesystem::file_size(artifact_path, ec);
    if (!ec) {
        telemetry::obs::EmitCounter("model_bytes_read", static_cast<long>(size), "bytes", "model",
                                    {}, {{"artifact_path", artifact_path}});
    }
    nlohmann::json end_fields = start_fields;
    end_fields["duration_ms"] = duration_ms;
    telemetry::obs::LogEvent(telemetry::obs::LogLevel::Info, "model_load_end", "model", end_fields);
}

auto PcaModel::Score(const FeatureVector& vec) const -> PcaScore {
    PcaScore result;
    if (!loaded_) { return result; }

    // Adapt FeatureVector to vector
    // Note: FeatureVector is std::array, guaranteed contiguous
    linalg::Vector x_raw(vec.data.begin(), vec.data.end());

    // 1. Standardize: (x - u) / s
    linalg::Vector x_scaled = vec_div(vec_sub(x_raw, cur_mean_), cur_scale_);

    // 2. PCA Project
    // PCA formula:
    // X_centered = X - pca_mean_
    // X_transformed = X_centered * components.T
    
    linalg::Vector x_centered = vec_sub(x_scaled, pca_mean_);
    linalg::Vector x_proj = linalg::matvec(components_, x_centered); // (k, d) * (d, 1) -> (k, 1)

    // 3. Reconstruct
    // X_recon_scaled = X_transformed * components + pca_mean_
    linalg::Matrix components_t = linalg::transpose(components_);
    linalg::Vector x_recon_centered_col = linalg::matvec(components_t, x_proj);
    linalg::Vector x_recon_scaled = vec_add(x_recon_centered_col, pca_mean_);

    // 4. Residual
    linalg::Vector diff = vec_sub(x_scaled, x_recon_scaled);
    result.reconstruction_error = linalg::l2_norm(diff); // L2 norm

    // Store residuals
    result.residuals.resize(FeatureVector::kSize);
    result.residuals = diff;

    if (result.reconstruction_error > threshold_) {
        result.is_anomaly = true;
        std::stringstream ss;
        ss << "PCA_RECON_ERR=" << result.reconstruction_error << " > " << threshold_;
        result.details = ss.str();
    }

    return result;
}

auto PcaModel::EstimateMemoryUsage() const -> size_t {
    size_t usage = sizeof(PcaModel);
    usage += cur_mean_.size() * sizeof(double);
    usage += cur_scale_.size() * sizeof(double);
    usage += components_.data.size() * sizeof(double);
    usage += pca_mean_.size() * sizeof(double);
    return usage;
}

} // namespace telemetry::anomaly
