#include "VirtualFileHandleManager.hpp"
#include "VirtualFile.hpp"
#include "ConnectionPool.hpp"
#include "PathRouter.hpp"

namespace sqlfuse {

VirtualFileHandleManager::VirtualFileHandleManager(SchemaManager& schema,
                                                   CacheManager& cache,
                                                   const DataConfig& config)
    : m_schema(schema), m_cache(cache), m_config(config) {
}

uint64_t VirtualFileHandleManager::create(const ParsedPath& path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    uint64_t handle = m_nextHandle++;

    m_handles[handle] = m_schema.connectionPool().createVirtualFile(
        path, m_schema, m_cache, m_config);

    return handle;
}

VirtualFile* VirtualFileHandleManager::get(uint64_t handle) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_handles.find(handle);
    if (it == m_handles.end()) {
        return nullptr;
    }

    return it->second.get();
}

void VirtualFileHandleManager::release(uint64_t handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handles.erase(handle);
}

size_t VirtualFileHandleManager::openCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_handles.size();
}

}  // namespace sqlfuse
