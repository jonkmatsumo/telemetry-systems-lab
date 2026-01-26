#include "training/pca_trainer.h"
#include <tuple>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <pqxx/strconv>

#include "contract.h"

namespace telemetry {
namespace training {

using json = nlohmann::json;

struct RunningStats {
    size_t n = 0;
    linalg::Vector mean;
    linalg::Matrix m2; // sum of squares of deviations

    explicit RunningStats(size_t dim) : mean(dim, 0.0), m2(dim, dim) {}

    void update(const linalg::Vector& x) {
        if (x.size() != mean.size()) {
            throw std::runtime_error("RunningStats dimension mismatch");
        }
        n += 1;
        linalg::Vector delta(x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            delta[i] = x[i] - mean[i];
            mean[i] += delta[i] / static_cast<double>(n);
        }
        linalg::Vector delta2(x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            delta2[i] = x[i] - mean[i];
        }
        for (size_t i = 0; i < x.size(); ++i) {
            for (size_t j = 0; j < x.size(); ++j) {
                m2(i, j) += delta[i] * delta2[j];
            }
        }
    }
};

static void stream_samples(const std::string& db_conn_str,
                           const std::string& dataset_id,
                           const std::function<void(const linalg::Vector&)>& on_sample) {
    pqxx::connection conn(db_conn_str);
    pqxx::work txn(conn);

    std::string query =
        "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate "
        "FROM host_telemetry_archival WHERE run_id = " + txn.quote(dataset_id) + " AND is_anomaly = false";
    pqxx::stream_from stream(txn, query);

    std::tuple<double, double, double, double, double> row_data;
    while (stream >> row_data) {
        auto [cpu, mem, disk, rx, tx] = row_data;
        linalg::Vector x(telemetry::anomaly::FeatureVector::kSize, 0.0);
        x[0] = cpu;
        x[1] = mem;
        x[2] = disk;
        x[3] = rx;
        x[4] = tx;
        on_sample(x);
    }

    stream.complete();
    txn.commit();
}

static linalg::Vector vec_sub(const linalg::Vector& a, const linalg::Vector& b) {
    if (a.size() != b.size()) throw std::runtime_error("vec_sub dimension mismatch");
    linalg::Vector out(a.size(), 0.0);
    for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] - b[i];
    return out;
}

static linalg::Vector vec_add(const linalg::Vector& a, const linalg::Vector& b) {
    if (a.size() != b.size()) throw std::runtime_error("vec_add dimension mismatch");
    linalg::Vector out(a.size(), 0.0);
    for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] + b[i];
    return out;
}

static linalg::Vector vec_div(const linalg::Vector& a, const linalg::Vector& b) {
    if (a.size() != b.size()) throw std::runtime_error("vec_div dimension mismatch");
    linalg::Vector out(a.size(), 0.0);
    for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] / b[i];
    return out;
}

static linalg::Vector vec_scale(const linalg::Vector& a, double s) {
    linalg::Vector out(a.size(), 0.0);
    for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] * s;
    return out;
}

static double percentile_value(std::vector<double> values, double percentile) {
    if (values.empty()) {
        throw std::runtime_error("percentile_value requires non-empty input");
    }
    std::sort(values.begin(), values.end());
    double rank = (percentile / 100.0) * static_cast<double>(values.size());
    size_t idx = 0;
    if (rank <= 1.0) {
        idx = 0;
    } else {
        idx = static_cast<size_t>(std::ceil(rank)) - 1;
        if (idx >= values.size()) idx = values.size() - 1;
    }
    return values[idx];
}

static void enforce_component_sign(linalg::Vector& v) {
    size_t idx = 0;
    double max_abs = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        double abs_val = std::abs(v[i]);
        if (abs_val > max_abs) {
            max_abs = abs_val;
            idx = i;
        }
    }
    if (v[idx] < 0.0) {
        for (double& val : v) {
            val *= -1.0;
        }
    }
}

static PcaArtifact TrainPcaFromStream(const std::function<void(const std::function<void(const linalg::Vector&)>&)>& for_each,
                                      size_t dim,
                                      int n_components,
                                      double percentile) {
    if (n_components <= 0) {
        throw std::runtime_error("n_components must be positive");
    }

    RunningStats stats(dim);
    for_each([&stats](const linalg::Vector& x) { stats.update(x); });

    if (stats.n < 2) {
        throw std::runtime_error("Not enough samples to train PCA");
    }

    linalg::Vector scale(dim, 0.0);
    for (size_t i = 0; i < dim; ++i) {
        double var_pop = stats.m2(i, i) / static_cast<double>(stats.n);
        double s = std::sqrt(var_pop);
        if (s == 0.0) s = 1.0;
        scale[i] = s;
    }

    linalg::Matrix cov(dim, dim);
    double denom = static_cast<double>(stats.n - 1);
    for (size_t i = 0; i < dim; ++i) {
        for (size_t j = 0; j < dim; ++j) {
            cov(i, j) = stats.m2(i, j) / denom;
            cov(i, j) /= (scale[i] * scale[j]);
        }
    }

    auto eig = linalg::eigen_sym_jacobi(cov, 200, 1e-12);
    auto order = linalg::argsort_desc(eig.eigenvalues);

    int k = std::min<int>(n_components, static_cast<int>(dim));
    linalg::Matrix components(static_cast<size_t>(k), dim);
    linalg::Vector explained_variance(static_cast<size_t>(k), 0.0);

    for (int i = 0; i < k; ++i) {
        size_t idx = order[static_cast<size_t>(i)];
        explained_variance[static_cast<size_t>(i)] = eig.eigenvalues[idx];
        linalg::Vector comp(dim, 0.0);
        for (size_t r = 0; r < dim; ++r) {
            comp[r] = eig.eigenvectors(r, idx);
        }
        enforce_component_sign(comp);
        for (size_t c = 0; c < dim; ++c) {
            components(static_cast<size_t>(i), c) = comp[c];
        }
    }

    linalg::Vector pca_mean(dim, 0.0);
    size_t count = 0;
    for_each([&](const linalg::Vector& x) {
        linalg::Vector x_scaled = vec_div(vec_sub(x, stats.mean), scale);
        pca_mean = vec_add(pca_mean, x_scaled);
        count += 1;
    });

    if (count == 0) {
        throw std::runtime_error("No samples found for PCA mean computation");
    }
    pca_mean = vec_scale(pca_mean, 1.0 / static_cast<double>(count));

    std::vector<double> errors;
    errors.reserve(count);
    for_each([&](const linalg::Vector& x) {
        linalg::Vector x_scaled = vec_div(vec_sub(x, stats.mean), scale);
        linalg::Vector x_centered = vec_sub(x_scaled, pca_mean);
        linalg::Vector x_proj = linalg::matvec(components, x_centered);
        linalg::Matrix components_t = linalg::transpose(components);
        linalg::Vector x_recon_centered = linalg::matvec(components_t, x_proj);
        linalg::Vector x_recon_scaled = vec_add(x_recon_centered, pca_mean);
        linalg::Vector diff = vec_sub(x_scaled, x_recon_scaled);
        errors.push_back(linalg::l2_norm(diff));
    });

    double threshold = percentile_value(errors, percentile);

    PcaArtifact artifact;
    artifact.scaler_mean = stats.mean;
    artifact.scaler_scale = scale;
    artifact.components = components;
    artifact.explained_variance = explained_variance;
    artifact.pca_mean = pca_mean;
    artifact.threshold = threshold;
    artifact.n_components = k;

    return artifact;
}

PcaArtifact TrainPcaFromDb(const std::string& db_conn_str,
                           const std::string& dataset_id,
                           int n_components,
                           double percentile) {
    auto for_each = [&](const std::function<void(const linalg::Vector&)>& cb) {
        stream_samples(db_conn_str, dataset_id, cb);
    };
    return TrainPcaFromStream(for_each, telemetry::anomaly::FeatureVector::kSize, n_components, percentile);
}

PcaArtifact TrainPcaFromSamples(const std::vector<linalg::Vector>& samples,
                                int n_components,
                                double percentile) {
    auto for_each = [&](const std::function<void(const linalg::Vector&)>& cb) {
        for (const auto& x : samples) {
            cb(x);
        }
    };
    return TrainPcaFromStream(for_each, telemetry::anomaly::FeatureVector::kSize, n_components, percentile);
}

void WriteArtifactJson(const PcaArtifact& artifact, const std::string& output_path) {
    json j;
    j["meta"]["version"] = "v1";
    j["meta"]["type"] = "pca_reconstruction";
    j["meta"]["features"] = telemetry::anomaly::FeatureMetadata::GetFeatureNames();

    j["preprocessing"]["mean"] = artifact.scaler_mean;
    j["preprocessing"]["scale"] = artifact.scaler_scale;

    std::vector<std::vector<double>> components_rows;
    components_rows.resize(artifact.components.rows);
    for (size_t r = 0; r < artifact.components.rows; ++r) {
        components_rows[r].resize(artifact.components.cols);
        for (size_t c = 0; c < artifact.components.cols; ++c) {
            components_rows[r][c] = artifact.components(r, c);
        }
    }

    j["model"]["components"] = components_rows;
    j["model"]["explained_variance"] = artifact.explained_variance;
    j["model"]["mean"] = artifact.pca_mean;
    j["model"]["n_components"] = artifact.n_components;

    j["thresholds"]["reconstruction_error"] = artifact.threshold;

    std::ofstream out(output_path);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open output path: " + output_path);
    }
    out << j.dump(4);
}

} // namespace training
} // namespace telemetry
