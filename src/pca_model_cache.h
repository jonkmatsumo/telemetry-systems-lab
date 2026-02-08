#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "detectors/pca_model.h"

namespace telemetry::anomaly {

/**
 * @brief Thread-safe in-memory cache for PCA models to avoid redundant artifact loading.
 */
class PcaModelCache {
public:
    /**
     * @param max_entries Maximum number of models to keep in memory.
     * @param max_bytes Maximum total memory footprint in bytes.
     * @param ttl_seconds Time-to-live for cache entries (fallback invalidation).
     */
    struct PcaModelCacheArgs {
        size_t max_entries = 100;
        size_t max_bytes = 512ull * 1024ull * 1024ull; // 512MB default
        int ttl_seconds = 3600;
    };

    explicit PcaModelCache(PcaModelCacheArgs args);
    PcaModelCache();

    /**
     * @brief Gets a model from cache or loads it from artifact_path if missing.
     */
    auto GetOrCreate(const std::string& model_run_id, 
                     const std::string& artifact_path) -> std::shared_ptr<PcaModel>;

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
        size_t bytes_used;
        size_t max_bytes;
        long long hits;
        long long misses;
        long long evictions;
    };
    auto GetStats() const -> CacheStats;

private:
    struct CacheEntry {
        std::shared_ptr<PcaModel> model;
        std::chrono::steady_clock::time_point last_access;
        std::string artifact_path;
        size_t memory_usage = 0;
    };

    void EvictLru();
    void EnsureCapacity(size_t additional_bytes);

    size_t max_entries_;
    size_t max_bytes_;
    size_t current_bytes_ = 0;
    std::chrono::seconds ttl_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;

    long long hits_ = 0;
    long long misses_ = 0;
    long long evictions_ = 0;
};

} // namespace telemetry::anomaly
