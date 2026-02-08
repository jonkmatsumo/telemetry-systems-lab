#include "pca_model_cache.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include "obs/metrics.h"

namespace telemetry::anomaly {

PcaModelCache::PcaModelCache(PcaModelCacheArgs args)
    : max_entries_(args.max_entries), max_bytes_(args.max_bytes), ttl_(args.ttl_seconds) {
    spdlog::info("Initialized PcaModelCache with max_entries={}, max_bytes={}, ttl={}s", 
                 max_entries_, max_bytes_, ttl_.count());
}

PcaModelCache::PcaModelCache() : PcaModelCache(PcaModelCacheArgs{}) {}

auto PcaModelCache::GetOrCreate(const std::string& model_run_id, 
                                     const std::string& artifact_path) -> std::shared_ptr<PcaModel> {
    std::unique_lock<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    auto it = cache_.find(model_run_id);
    if (it != cache_.end()) {
        // Check TTL
        if (now - it->second.last_access > ttl_) {
            spdlog::debug("Cache TTL expired for model {}", model_run_id);
            current_bytes_ -= it->second.memory_usage;
            cache_.erase(it);
        } else if (it->second.artifact_path == artifact_path) {
            hits_++;
            it->second.last_access = now;
            telemetry::obs::EmitCounter("model_cache_hits", 1, "hits", "model_cache");
            return it->second.model;
        } else {
            // Path changed? Unexpected but handled by reload
            spdlog::warn("Artifact path mismatch for model {}. Cache: {}, Requested: {}. Reloading.",
                         model_run_id, it->second.artifact_path, artifact_path);
            current_bytes_ -= it->second.memory_usage;
            cache_.erase(it);
        }
    }

    misses_++;
    telemetry::obs::EmitCounter("model_cache_misses", 1, "misses", "model_cache");
    lock.unlock(); // Unlock while loading artifact to avoid blocking other cache accesses

    auto model = std::make_shared<PcaModel>();
    try {
        model->Load(artifact_path);
        telemetry::obs::EmitCounter("model_load_count", 1, "loads", "model_cache");
    } catch (const std::exception& e) {
        spdlog::error("Failed to load model {} from {}: {}", model_run_id, artifact_path, e.what());
        throw;
    }

    size_t usage = model->EstimateMemoryUsage();

    lock.lock();
    
    // Check if single model is too large
    if (usage > max_bytes_) {
        spdlog::error("Model {} is too large for cache ({} > {} bytes). Not caching.", 
                      model_run_id, usage, max_bytes_);
        return model; // Return but don't cache
    }

    EnsureCapacity(usage);

    if (cache_.size() >= max_entries_) {
        EvictLru();
    }

    cache_[model_run_id] = {model, now, artifact_path, usage};
    current_bytes_ += usage;
    
    telemetry::obs::EmitGauge("model_cache_bytes_used", static_cast<double>(current_bytes_), "bytes", "model_cache");
    telemetry::obs::EmitGauge("model_cache_entries", static_cast<double>(cache_.size()), "entries", "model_cache");

    return model;
}

auto PcaModelCache::Invalidate(const std::string& model_run_id) -> void {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(model_run_id);
    if (it != cache_.end()) {
        current_bytes_ -= it->second.memory_usage;
        cache_.erase(it);
        telemetry::obs::EmitGauge("model_cache_bytes_used", static_cast<double>(current_bytes_), "bytes", "model_cache");
    }
}

auto PcaModelCache::Clear() -> void {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    current_bytes_ = 0;
    telemetry::obs::EmitGauge("model_cache_bytes_used", 0.0, "bytes", "model_cache");
}

auto PcaModelCache::EnsureCapacity(size_t additional_bytes) -> void {
    while (!cache_.empty() && (current_bytes_ + additional_bytes > max_bytes_)) {
        EvictLru();
    }
}

auto PcaModelCache::EvictLru() -> void {
    if (cache_.empty()) { return; }

    auto oldest = cache_.begin();
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->second.last_access < oldest->second.last_access) {
            oldest = it;
        }
    }

    spdlog::debug("Evicting model {} from cache ({} bytes)", oldest->first, oldest->second.memory_usage);
    current_bytes_ -= oldest->second.memory_usage;
    cache_.erase(oldest);
    evictions_++;
    telemetry::obs::EmitCounter("model_cache_evictions", 1, "evictions", "model_cache");
}

auto PcaModelCache::GetStats() const -> PcaModelCache::CacheStats {
    std::lock_guard<std::mutex> lock(mutex_);
    return {
        cache_.size(),
        current_bytes_,
        max_bytes_,
        hits_,
        misses_,
        evictions_
    };
}

} // namespace telemetry::anomaly
