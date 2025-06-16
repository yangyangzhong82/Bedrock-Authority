#include "permission/AsyncCacheInvalidator.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include "permission/PermissionCache.h"
#include "permission/PermissionStorage.h"



namespace BA {
namespace permission {
namespace internal {

using namespace std;

AsyncCacheInvalidator::AsyncCacheInvalidator(PermissionCache& cache, PermissionStorage& storage)
: m_cache(cache),
  m_storage(storage) {}

AsyncCacheInvalidator::~AsyncCacheInvalidator() {
    // Ensure threads are stopped if not already.
    if (m_running) {
        stop();
    }
}

void AsyncCacheInvalidator::start(unsigned int threadPoolSize) {
    if (m_running) {
        return; // Already running
    }
    m_running = true;
    for (unsigned int i = 0; i < threadPoolSize; ++i) {
        m_workerThreads.emplace_back(&AsyncCacheInvalidator::processTasks, this);
    }
    ::ll::mod::NativeMod::current()->getLogger().info("AsyncCacheInvalidator started with {} threads.", threadPoolSize);
}

void AsyncCacheInvalidator::stop() {
    if (!m_running) {
        return; // Already stopped
    }

    // Enqueue a shutdown task for each worker thread
    if (!m_workerThreads.empty()) {
        {
            unique_lock<mutex> lock(m_queueMutex);
            for (size_t i = 0; i < m_workerThreads.size(); ++i) {
                m_taskQueue.push({CacheInvalidationTaskType::SHUTDOWN, ""});
            }
        }
        m_condition.notify_all(); // Wake up all threads to process shutdown tasks
    }

    m_running = false; // Signal that no more new tasks should be processed

    // Join all threads
    for (std::thread& worker : m_workerThreads) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workerThreads.clear();
    ::ll::mod::NativeMod::current()->getLogger().info("AsyncCacheInvalidator stopped.");
}

void AsyncCacheInvalidator::enqueueTask(CacheInvalidationTask task) {
    if (!m_running) {
        ::ll::mod::NativeMod::current()->getLogger().warn("AsyncCacheInvalidator is not running, task dropped.");
        return;
    }

    // 扩大 m_pendingTasksMutex 的锁定范围，使其包含对 m_taskQueue 的操作
    {
        unique_lock<mutex> pendingLock(m_pendingTasksMutex); // 先锁定 m_pendingTasksMutex
        // 任务合并逻辑
        if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
            if (m_allGroupsModifiedPending || m_pendingGroupModifiedTasks.count(task.data)) {
                // 已合并
                return;
            }
            m_pendingGroupModifiedTasks.insert(task.data);
        } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
            if (m_allGroupsModifiedPending) {
                // 已合并
                return;
            }
            m_pendingGroupModifiedTasks.clear(); // ALL 任务包含所有特定组修改
            m_allGroupsModifiedPending = true;
        }

        // 在 m_pendingTasksMutex 保护下将任务推入队列
        unique_lock<mutex> queueLock(m_queueMutex); // 然后锁定 m_queueMutex
        m_taskQueue.push(std::move(task));
    }
    m_condition.notify_one();
}

void AsyncCacheInvalidator::processTasks() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    while (true) {
        CacheInvalidationTask task;
        {
            // 统一锁顺序：先获取 m_pendingTasksMutex，再获取 m_queueMutex
            unique_lock<mutex> pendingLock(m_pendingTasksMutex); // 锁A
            unique_lock<mutex> queueLock(m_queueMutex);          // 锁B

            // 等待队列非空
            m_condition.wait(queueLock, [this] { return !m_taskQueue.empty(); });

            // 从队列中取出任务
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();

            // 在 m_pendingTasksMutex 保护下更新待处理任务状态
            // 此时 m_pendingTasksMutex 已经被持有，所以可以直接修改
            if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
                m_pendingGroupModifiedTasks.erase(task.data);
            } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
                m_allGroupsModifiedPending = false;
            }
        } // pendingLock 和 queueLock 在这里同时释放

        // Handle SHUTDOWN task first
        if (task.type == CacheInvalidationTaskType::SHUTDOWN) {
            logger.debug("Worker thread received SHUTDOWN task and is exiting.");
            break; // Exit the loop and terminate the thread
        }

        try {
            switch (task.type) {
            case CacheInvalidationTaskType::GROUP_MODIFIED: {
                logger.debug("Worker: Processing GROUP_MODIFIED for group '{}'", task.data);
                // A group's permissions or inheritance changed.
                // This affects the group itself, all its children, and all players in these groups.
                set<string> affectedGroups = m_cache.getChildGroupsRecursive(task.data);
                for (const auto& groupName : affectedGroups) {
                    m_cache.invalidateGroupPermissions(groupName);

                    vector<string> affectedPlayers = getAffectedPlayersByGroup(groupName);
                    for (const auto& playerUuid : affectedPlayers) {
                        m_cache.invalidatePlayerPermissions(playerUuid);
                        // Player's group list doesn't change, so no need to invalidate player groups cache here.
                    }
                }
                break;
            }
            case CacheInvalidationTaskType::PLAYER_GROUP_CHANGED: {
                logger.debug("Worker: Processing PLAYER_GROUP_CHANGED for player '{}'", task.data);
                // A player was added/removed from a group.
                m_cache.invalidatePlayerPermissions(task.data);
                m_cache.invalidatePlayerGroups(task.data);
                break;
            }
            case CacheInvalidationTaskType::ALL_GROUPS_MODIFIED: {
                logger.debug("Worker: Processing ALL_GROUPS_MODIFIED");
                // A global change, like a new permission registration.
                m_cache.invalidateAllGroupPermissions();
                m_cache.invalidateAllPlayerPermissions();
                break;
            }
            case CacheInvalidationTaskType::ALL_PLAYERS_MODIFIED: {
                logger.debug("Worker: Processing ALL_PLAYERS_MODIFIED");
                // A change affecting all players, like a default permission value change.
                m_cache.invalidateAllPlayerPermissions();
                break;
            }
            case CacheInvalidationTaskType::SHUTDOWN:
                // This case is handled at the beginning of the loop
                break;
            }
        } catch (const std::exception& e) {
            logger.error("Async task processing failed with exception: {}", e.what());
        } catch (...) {
            logger.error("Async task processing failed with an unknown exception.");
        }
    }
}

// Helper to find all players affected by a group change.
// It queries the storage layer because the cache might not have all player data.
vector<string> AsyncCacheInvalidator::getAffectedPlayersByGroup(const string& groupName) {
    auto&       logger = ::ll::mod::NativeMod::current()->getLogger();
    set<string> affectedPlayerUuids;

    // Get all descendant groups (including the group itself) from the cache.
    // The inheritance cache is assumed to be reliable.
    set<string> allRelatedGroups = m_cache.getChildGroupsRecursive(groupName);

    logger.debug(
        "Worker: Finding affected players for group '{}'. Related groups count: {}",
        groupName,
        allRelatedGroups.size()
    );

    // For each related group, get its members from the storage layer.
    // First, collect all group names to query their IDs in bulk.
    set<string> groupNamesToFetchIds;
    for (const auto& groupName : allRelatedGroups) {
        groupNamesToFetchIds.insert(groupName);
    }

    // Bulk fetch group IDs
    unordered_map<string, string> groupNameToIdMap = m_storage.fetchGroupIdsByNames(groupNamesToFetchIds);

    vector<string> groupIdsToFetchPlayers;
    for (const auto& groupName : allRelatedGroups) {
        auto it = groupNameToIdMap.find(groupName);
        if (it != groupNameToIdMap.end()) {
            groupIdsToFetchPlayers.push_back(it->second);
        }
    }

    // Bulk fetch players in these groups
    if (!groupIdsToFetchPlayers.empty()) {
        vector<string> playersInGroups = m_storage.fetchPlayersInGroups(groupIdsToFetchPlayers);
        affectedPlayerUuids.insert(playersInGroups.begin(), playersInGroups.end());
    }

    return vector<string>(affectedPlayerUuids.begin(), affectedPlayerUuids.end());
}


} // namespace internal
} // namespace permission
} // namespace BA
