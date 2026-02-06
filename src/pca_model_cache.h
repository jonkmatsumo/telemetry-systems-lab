#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "detectors/pca_model.h"

namespace telemetry {
namespace anomaly {

/**
 * @brief Thread-safe in-memory cache for PCA models to avoid redundant artifact loading.
 */
class PcaModelCache {
public:
    /**
     * @param max_entries Maximum number of models to keep in memory.
     * @param ttl_seconds Time-to-live for cache entries (fallback invalidation).
     */
    explicit PcaModelCache(size_t max_entries = 100, int ttl_seconds = 3600);

    /**
     * @brief Gets a model from cache or loads it from artifact_path if missing.
     */
    std::shared_ptr<PcaModel> GetOrCreate(const std::string& model_run_id, 
                                          const std::string& artifact_path);

    /**
     * @brief Explicitly removes an entry from the cache.
     */
    void Invalidate(const std::string& model_run_id);

    /**
     * @brief Clears all entries.
     */
    void Clear();

    struct CacheStats {
        size_t size;
        long long hits;
        long long misses;
        long long evictions;
    };
    CacheStats GetStats() const;

private:
    struct CacheEntry {
        std::shared_ptr<PcaModel> model;
        std::chrono::steady_clock::time_point last_access;
        std::string artifact_path;
    };

    void EvictLru();

    size_t max_entries_;
    std::chrono::seconds ttl_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;

    long long hits_ = 0;
    long long misses_ = 0;
    long long evictions_ = 0;
};

} // namespace anomaly
} // namespace telemetry
