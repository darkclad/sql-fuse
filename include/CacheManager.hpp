#pragma once

#include "Config.hpp"
#include <string>
#include <optional>
#include <unordered_map>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <functional>
#include <regex>

namespace sqlfuse {

class CacheManager {
public:
    struct CacheEntry {
        std::string data;
        std::chrono::steady_clock::time_point created;
        std::chrono::steady_clock::time_point expires;
        size_t size;
        uint64_t hits = 0;
    };

    struct Stats {
        uint64_t hits = 0;
        uint64_t misses = 0;
        size_t currentSize = 0;
        size_t maxSize = 0;
        size_t entryCount = 0;
        size_t evictions = 0;
    };

    explicit CacheManager(const CacheConfig& config);
    ~CacheManager() = default;

    // Non-copyable
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    // Get cached value
    std::optional<std::string> get(const std::string& key);

    // Store value with optional custom TTL
    void put(const std::string& key, std::string data,
             std::optional<std::chrono::seconds> ttl = std::nullopt);

    // Store with category-based TTL
    enum class Category {
        Schema,
        Metadata,
        Data
    };
    void put(const std::string& key, std::string data, Category category);

    // Check if key exists and is not expired
    bool contains(const std::string& key);

    // Remove specific key
    void remove(const std::string& key);

    // Invalidate entries matching pattern (supports * wildcard)
    void invalidate(const std::string& pattern);

    // Invalidate entries for a specific database/table
    void invalidateTable(const std::string& database, const std::string& table);
    void invalidateDatabase(const std::string& database);

    // Clear all entries
    void clear();

    // Remove expired entries
    void pruneExpired();

    // Get statistics
    Stats getStats() const;

    // Build cache key helpers
    static std::string makeKey(const std::string& database,
                               const std::string& object,
                               const std::string& suffix = "");

private:
    void evictLRU();
    void evictIfNeeded(size_t requiredSpace);
    std::chrono::seconds getTTLForCategory(Category category) const;
    bool matchesPattern(const std::string& key, const std::string& pattern) const;

    CacheConfig m_config;

    std::unordered_map<std::string, CacheEntry> m_cache;
    std::list<std::string> m_lruList;
    std::unordered_map<std::string, std::list<std::string>::iterator> m_lruMap;

    mutable std::shared_mutex m_mutex;

    Stats m_stats;
};

}  // namespace sqlfuse
