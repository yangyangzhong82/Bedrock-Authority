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

    {
        unique_lock<mutex> lock(m_queueMutex);
        // Task coalescing logic
        {
            unique_lock<mutex> pendingLock(m_pendingTasksMutex);
            if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
                if (m_allGroupsModifiedPending || m_pendingGroupModifiedTasks.count(task.data)) {
                    // Merged
                    return;
                }
                m_pendingGroupModifiedTasks.insert(task.data);
            } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
                if (m_allGroupsModifiedPending) {
                    // Merged
                    return;
                }
                m_pendingGroupModifiedTasks.clear(); // ALL subsumes specific group modifications
                m_allGroupsModifiedPending = true;
            }
        }
        m_taskQueue.push(std::move(task));
    }
    m_condition.notify_one();
}

void AsyncCacheInvalidator::processTasks() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    while (true) {
        CacheInvalidationTask task;
        {
            unique_lock<mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this] { return !m_taskQueue.empty(); });

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        // Handle SHUTDOWN task first
        if (task.type == CacheInvalidationTaskType::SHUTDOWN) {
            logger.debug("Worker thread received SHUTDOWN task and is exiting.");
            break; // Exit the loop and terminate the thread
        }

        // Update pending tasks state after dequeuing
        {
            unique_lock<mutex> pendingLock(m_pendingTasksMutex);
            if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
                m_pendingGroupModifiedTasks.erase(task.data);
            } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
                m_allGroupsModifiedPending = false;
            }
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
    for (const auto& relatedGroupName : allRelatedGroups) {
        // We need the group ID to query the player_groups table
        string groupId = m_storage.fetchGroupIdByName(relatedGroupName);
        if (groupId.empty()) {
            continue;
        }

        vector<string> playersInGroup = m_storage.fetchPlayersInGroup(groupId);
        affectedPlayerUuids.insert(playersInGroup.begin(), playersInGroup.end());
    }

    return vector<string>(affectedPlayerUuids.begin(), affectedPlayerUuids.end());
}


} // namespace internal
} // namespace permission
} // namespace BA