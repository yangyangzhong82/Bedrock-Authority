#pragma once

#include "permission/PermissionData.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <vector>


namespace BA {
namespace permission {
namespace internal {

class PermissionCache;   // Forward declaration
class PermissionStorage; // Forward declaration

class AsyncCacheInvalidator {
public:
    AsyncCacheInvalidator(PermissionCache& cache, PermissionStorage& storage);
    ~AsyncCacheInvalidator();

    void start(unsigned int threadPoolSize);
    void stop();
    void enqueueTask(CacheInvalidationTask task);

private:
    void                     processTasks();
    std::vector<std::string> getAffectedPlayersByGroup(const std::string& groupName);

    PermissionCache&   m_cache;
    PermissionStorage& m_storage;

    std::queue<CacheInvalidationTask> m_taskQueue;
    std::mutex                        m_queueMutex;
    std::condition_variable           m_condition;
    std::vector<std::thread>          m_workerThreads;
    std::atomic<bool>                 m_running = false;

    // Task coalescing members
    std::mutex            m_pendingTasksMutex;
    std::set<std::string> m_pendingGroupModifiedTasks;
    bool                  m_allGroupsModifiedPending = false;
};

} // namespace internal
} // namespace permission
} // namespace BA