#include "pca_model_cache.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace telemetry {
namespace anomaly {

PcaModelCache::PcaModelCache(size_t max_entries, int ttl_seconds)
    : max_entries_(max_entries), ttl_(ttl_seconds) {
    spdlog::info("Initialized PcaModelCache with max_entries={}, ttl={}s", max_entries_, ttl_seconds);
}

std::shared_ptr<PcaModel> PcaModelCache::GetOrCreate(const std::string& model_run_id, 
                                                    const std::string& artifact_path) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    auto it = cache_.find(model_run_id);
    if (it != cache_.end()) {
        // Check TTL
        if (now - it->second.last_access > ttl_) {
            spdlog::debug("Cache TTL expired for model {}", model_run_id);
            cache_.erase(it);
        } else if (it->second.artifact_path == artifact_path) {
            hits_++;
            it->second.last_access = now;
            return it->second.model;
        } else {
            // Path changed? Unexpected but handled by reload
            spdlog::warn("Artifact path mismatch for model {}. Cache: {}, Requested: {}. Reloading.",
                         model_run_id, it->second.artifact_path, artifact_path);
            cache_.erase(it);
        }
    }

    misses_++;
    lock.unlock(); // Unlock while loading artifact to avoid blocking other cache accesses

    auto model = std::make_shared<PcaModel>();
    try {
        model->Load(artifact_path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load model {} from {}: {}", model_run_id, artifact_path, e.what());
        throw;
    }

    lock.lock();
    if (cache_.size() >= max_entries_) {
        EvictLru();
    }

    cache_[model_run_id] = {model, now, artifact_path};
    return model;
}

void PcaModelCache::Invalidate(const std::string& model_run_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(model_run_id);
}

void PcaModelCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

void PcaModelCache::EvictLru() {
    if (cache_.empty()) return;

    auto oldest = cache_.begin();
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->second.last_access < oldest->second.last_access) {
            oldest = it;
        }
    }

    spdlog::debug("Evicting model {} from cache", oldest->first);
    cache_.erase(oldest);
    evictions_++;
}

PcaModelCache::CacheStats PcaModelCache::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {
        cache_.size(),
        hits_,
        misses_,
        evictions_
    };
}

} // namespace anomaly
} // namespace telemetry
