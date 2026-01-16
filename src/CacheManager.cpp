#include "CacheManager.hpp"
#include <spdlog/spdlog.h>
#include <regex>

namespace sqlfuse {

CacheManager::CacheManager(const CacheConfig& config)
    : m_config(config) {
    m_stats.maxSize = config.max_size_bytes;
}

std::optional<std::string> CacheManager::get(const std::string& key) {
    if (!m_config.enabled) {
        return std::nullopt;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        m_stats.misses++;
        return std::nullopt;
    }

    // Check expiration
    auto now = std::chrono::steady_clock::now();
    if (now >= it->second.expires) {
        // Expired - remove it
        m_stats.currentSize -= it->second.size;
        m_lruList.erase(m_lruMap[key]);
        m_lruMap.erase(key);
        m_cache.erase(it);
        m_stats.misses++;
        return std::nullopt;
    }

    // Update LRU
    m_lruList.erase(m_lruMap[key]);
    m_lruList.push_front(key);
    m_lruMap[key] = m_lruList.begin();

    it->second.hits++;
    m_stats.hits++;

    return it->second.data;
}

void CacheManager::put(const std::string& key, std::string data,
                       std::optional<std::chrono::seconds> ttl) {
    if (!m_config.enabled) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    size_t data_size = data.size();

    // Evict if necessary
    evictIfNeeded(data_size);

    auto now = std::chrono::steady_clock::now();
    std::chrono::seconds actual_ttl = ttl.value_or(m_config.data_ttl);

    // Check if key already exists
    auto existing = m_cache.find(key);
    if (existing != m_cache.end()) {
        m_stats.currentSize -= existing->second.size;
        m_lruList.erase(m_lruMap[key]);
    }

    CacheEntry entry{
        .data = std::move(data),
        .created = now,
        .expires = now + actual_ttl,
        .size = data_size,
        .hits = 0
    };

    m_cache[key] = std::move(entry);
    m_lruList.push_front(key);
    m_lruMap[key] = m_lruList.begin();

    m_stats.currentSize += data_size;
    m_stats.entryCount = m_cache.size();

    spdlog::debug("Cached '{}' ({} bytes, TTL {}s)", key, data_size, actual_ttl.count());
}

void CacheManager::put(const std::string& key, std::string data, Category category) {
    put(key, std::move(data), getTTLForCategory(category));
}

bool CacheManager::contains(const std::string& key) {
    if (!m_config.enabled) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(m_mutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        return false;
    }

    // Check expiration
    auto now = std::chrono::steady_clock::now();
    return now < it->second.expires;
}

void CacheManager::remove(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_stats.currentSize -= it->second.size;
        m_lruList.erase(m_lruMap[key]);
        m_lruMap.erase(key);
        m_cache.erase(it);
        m_stats.entryCount = m_cache.size();
    }
}

void CacheManager::invalidate(const std::string& pattern) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    std::vector<std::string> to_remove;

    for (const auto& [key, entry] : m_cache) {
        if (matchesPattern(key, pattern)) {
            to_remove.push_back(key);
        }
    }

    for (const auto& key : to_remove) {
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            m_stats.currentSize -= it->second.size;
            m_lruList.erase(m_lruMap[key]);
            m_lruMap.erase(key);
            m_cache.erase(it);
        }
    }

    m_stats.entryCount = m_cache.size();

    if (!to_remove.empty()) {
        spdlog::debug("Invalidated {} entries matching '{}'", to_remove.size(), pattern);
    }
}

void CacheManager::invalidateTable(const std::string& database, const std::string& table) {
    invalidate(database + "/" + table + "/*");
    invalidate(database + "/tables/" + table + "*");
}

void CacheManager::invalidateDatabase(const std::string& database) {
    invalidate(database + "/*");
}

void CacheManager::clear() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    m_cache.clear();
    m_lruList.clear();
    m_lruMap.clear();

    m_stats.currentSize = 0;
    m_stats.entryCount = 0;

    spdlog::debug("Cache cleared");
}

void CacheManager::pruneExpired() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> expired;

    for (const auto& [key, entry] : m_cache) {
        if (now >= entry.expires) {
            expired.push_back(key);
        }
    }

    for (const auto& key : expired) {
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            m_stats.currentSize -= it->second.size;
            m_lruList.erase(m_lruMap[key]);
            m_lruMap.erase(key);
            m_cache.erase(it);
        }
    }

    m_stats.entryCount = m_cache.size();

    if (!expired.empty()) {
        spdlog::debug("Pruned {} expired entries", expired.size());
    }
}

CacheManager::Stats CacheManager::getStats() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_stats;
}

std::string CacheManager::makeKey(const std::string& database,
                                   const std::string& object,
                                   const std::string& suffix) {
    std::string key = database;
    if (!object.empty()) {
        key += "/" + object;
    }
    if (!suffix.empty()) {
        key += "/" + suffix;
    }
    return key;
}

void CacheManager::evictLRU() {
    if (m_lruList.empty()) {
        return;
    }

    std::string key = m_lruList.back();
    m_lruList.pop_back();

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_stats.currentSize -= it->second.size;
        m_cache.erase(it);
    }

    m_lruMap.erase(key);
    m_stats.evictions++;
}

void CacheManager::evictIfNeeded(size_t required_space) {
    // Don't cache items larger than half the max size
    if (required_space > m_config.max_size_bytes / 2) {
        spdlog::debug("Item too large to cache ({} bytes)", required_space);
        return;
    }

    while (m_stats.currentSize + required_space > m_config.max_size_bytes && !m_lruList.empty()) {
        evictLRU();
    }
}

std::chrono::seconds CacheManager::getTTLForCategory(Category category) const {
    switch (category) {
        case Category::Schema:
            return m_config.schema_ttl;
        case Category::Metadata:
            return m_config.metadata_ttl;
        case Category::Data:
        default:
            return m_config.data_ttl;
    }
}

bool CacheManager::matchesPattern(const std::string& key, const std::string& pattern) const {
    // Simple wildcard matching with * at end
    if (pattern.empty()) {
        return true;
    }

    if (pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return key.compare(0, prefix.size(), prefix) == 0;
    }

    // Support * in the middle too
    size_t star_pos = pattern.find('*');
    if (star_pos != std::string::npos) {
        std::string before = pattern.substr(0, star_pos);
        std::string after = pattern.substr(star_pos + 1);

        if (!before.empty() && key.compare(0, before.size(), before) != 0) {
            return false;
        }
        if (!after.empty()) {
            if (key.size() < after.size()) return false;
            return key.compare(key.size() - after.size(), after.size(), after) == 0;
        }
        return true;
    }

    return key == pattern;
}

}  // namespace sqlfuse
