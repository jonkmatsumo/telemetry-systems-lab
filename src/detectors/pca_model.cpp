#include "pca_model.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace telemetry {
namespace anomaly {

using json = nlohmann::json;

void PcaModel::Load(const std::string& artifact_path) {
    std::ifstream f(artifact_path);
    if (!f.is_open()) {
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

    cur_mean_ = Eigen::Map<Eigen::VectorXd>(raw_mean.data(), raw_mean.size());
    cur_scale_ = Eigen::Map<Eigen::VectorXd>(raw_scale.data(), raw_scale.size());

    // 2. PCA
    // Components are stored as list of lists (k x d)
    auto raw_components = j["model"]["components"].get<std::vector<std::vector<double>>>();
    int k = raw_components.size();
    if (k == 0) throw std::runtime_error("No PCA components found");
    int d = raw_components[0].size();
    if (d != FeatureVector::kSize) throw std::runtime_error("Dimension mismatch in PCA components");

    // Copy to Eigen Matrix
    components_.resize(k, d);
    for (int i = 0; i < k; ++i) {
        for (int c = 0; c < d; ++c) {
            components_(i, c) = raw_components[i][c];
        }
    }

    auto raw_pca_mean = j["model"]["mean"].get<std::vector<double>>();
    pca_mean_ = Eigen::Map<Eigen::VectorXd>(raw_pca_mean.data(), raw_pca_mean.size());

    // 3. Thresholds
    threshold_ = j["thresholds"]["reconstruction_error"].get<double>();

    loaded_ = true;
    spdlog::info("PcaModel loaded from {}. Dimensions: {}x{}, Threshold: {}", 
        artifact_path, k, d, threshold_);
}

PcaScore PcaModel::Score(const FeatureVector& vec) const {
    PcaScore result;
    if (!loaded_) return result;

    // Adapt FeatureVector to Eigen
    // Note: FeatureVector is std::array, guaranteed contiguous
    Eigen::Map<const Eigen::VectorXd> x_raw(vec.data.data(), FeatureVector::kSize);

    // 1. Standardize: (x - u) / s
    // Array-wise operations
    Eigen::VectorXd x_scaled = (x_raw - cur_mean_).array() / cur_scale_.array();

    // 2. PCA Project
    // PCA formula in sklearn:
    // X_centered = X - pca_mean_
    // X_transformed = X_centered * components.T
    
    Eigen::VectorXd x_centered = x_scaled - pca_mean_;
    Eigen::VectorXd x_proj = components_ * x_centered; // (k, d) * (d, 1) -> (k, 1)

    // 3. Reconstruct
    // X_recon_scaled = X_transformed * components + pca_mean_
    Eigen::VectorXd x_recon_centered = x_proj.transpose() * components_; // (1, k) * (k, d) -> (1, d). Transpose back to column vector?
    // Let's stick to column vectors in Eigen logic consistent with matrix mult:
    // x_proj is (k, 1). components is (k, d).
    // Reconstruct = (k,1)' * (k,d) ? No. 
    // Sklearn: inverse_transform(X) = X @ components + mean
    // checking dims: (n, k) @ (k, d) -> (n, d).
    // For single vector (1, k) @ (k, d) -> (1, d).
    
    // Here we have x_proj as VectorXd (k).
    // so x_recon_centered = components^T * x_proj
    // (d, k) * (k, 1) -> (d, 1)
    Eigen::VectorXd x_recon_centered_col = components_.transpose() * x_proj;
    
    Eigen::VectorXd x_recon_scaled = x_recon_centered_col + pca_mean_;

    // 4. Residual
    Eigen::VectorXd diff = x_scaled - x_recon_scaled;
    result.reconstruction_error = diff.norm(); // L2 norm

    // Store residuals
    result.residuals.resize(FeatureVector::kSize);
    Eigen::VectorXd::Map(result.residuals.data(), result.residuals.size()) = diff;

    if (result.reconstruction_error > threshold_) {
        result.is_anomaly = true;
        std::stringstream ss;
        ss << "PCA_RECON_ERR=" << result.reconstruction_error << " > " << threshold_;
        result.details = ss.str();
    }

    return result;
}

} // namespace anomaly
} // namespace telemetry
