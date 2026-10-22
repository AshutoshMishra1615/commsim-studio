#pragma once
/**
 * engine/params.hpp  --  M1: thread-safe parameter store.
 *
 * Blocks call get(key, default) from the sim thread.
 * The UI / CLI calls set(key, value) from any thread.
 * A std::shared_mutex protects the map; reads are concurrent, writes lock.
 */
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace engine {

class ParamStore {
public:
    void set(const std::string& key, double value) {
        std::unique_lock lock(mu_);
        map_[key] = value;
    }

    /// Returns value for key, or default_val if not present.
    double get(const std::string& key, double default_val = 0.0) const {
        std::shared_lock lock(mu_);
        auto it = map_.find(key);
        return it != map_.end() ? it->second : default_val;
    }

    bool has(const std::string& key) const {
        std::shared_lock lock(mu_);
        return map_.count(key) > 0;
    }

private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, double> map_;
};

}  // namespace engine
