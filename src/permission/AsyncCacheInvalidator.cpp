#include "permission/AsyncCacheInvalidator.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include "permission/PermissionCache.h"
#include "permission/PermissionStorage.h"
#include <set>           // 显式包含 set
#include <stdexcept>     // 显式包含 stdexcept 用于 std::exception
#include <string>        // 显式包含 string
#include <unordered_map> // 显式包含 unordered_map
#include <utility>       // 显式包含 utility 用于 std::move
#include <vector>        // 显式包含 vector


namespace BA {
namespace permission {
namespace internal {

// 移除了 'using namespace std;' 以显式限定标准库类型。

/**
 * @brief 异步缓存失效器的构造函数。
 * @param cache 权限缓存的引用。
 * @param storage 权限存储的引用。
 */
AsyncCacheInvalidator::AsyncCacheInvalidator(PermissionCache& cache, PermissionStorage& storage)
: m_cache(cache),
  m_storage(storage) {
    ::ll::mod::NativeMod::current()->getLogger().debug("AsyncCacheInvalidator: 构造函数被调用。");
}

/**
 * @brief 异步缓存失效器的析构函数。确保在对象销毁时停止所有工作线程。
 */
AsyncCacheInvalidator::~AsyncCacheInvalidator() {
    // 确保线程已停止，如果尚未停止。
    if (m_running) {
        stop();
    }
    ::ll::mod::NativeMod::current()->getLogger().debug("AsyncCacheInvalidator: 析构函数被调用。");
}

/**
 * @brief 启动异步缓存失效器。
 * @param threadPoolSize 线程池中工作线程的数量。
 */
void AsyncCacheInvalidator::start(unsigned int threadPoolSize) {
    if (m_running) {
        ::ll::mod::NativeMod::current()->getLogger().warn("AsyncCacheInvalidator: 已经运行，无需再次启动。");
        return; // 已经运行
    }
    m_running = true;
    for (unsigned int i = 0; i < threadPoolSize; ++i) {
        m_workerThreads.emplace_back(&AsyncCacheInvalidator::processTasks, this);
        ::ll::mod::NativeMod::current()->getLogger().debug("AsyncCacheInvalidator: 启动工作线程 #{}。", i + 1);
    }
    ::ll::mod::NativeMod::current()->getLogger().info(
        "AsyncCacheInvalidator: 已启动，线程池大小为 {}。",
        threadPoolSize
    );
}

/**
 * @brief 停止异步缓存失效器，并等待所有工作线程完成。
 */
void AsyncCacheInvalidator::stop() {
    if (!m_running) {
        ::ll::mod::NativeMod::current()->getLogger().warn("AsyncCacheInvalidator: 已经停止，无需再次停止。");
        return; // 已经停止
    }

    // 为每个工作线程入队一个关闭任务
    if (!m_workerThreads.empty()) {
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            for (size_t i = 0; i < m_workerThreads.size(); ++i) {
                m_taskQueue.push({CacheInvalidationTaskType::SHUTDOWN, ""});
            }
            ::ll::mod::NativeMod::current()->getLogger().debug("AsyncCacheInvalidator: 已为所有工作线程入队关闭任务。");
        }
        m_condition.notify_all(); // 唤醒所有线程以处理关闭任务
    }

    m_running = false; // 发出信号，不再处理新的任务

    // 等待所有线程完成
    for (std::thread& worker : m_workerThreads) {
        if (worker.joinable()) {
            worker.join();
            ::ll::mod::NativeMod::current()->getLogger().debug("AsyncCacheInvalidator: 工作线程已加入。");
        }
    }
    m_workerThreads.clear();
    ::ll::mod::NativeMod::current()->getLogger().info("AsyncCacheInvalidator: 已停止。");
}

/**
 * @brief 将一个缓存失效任务加入队列。
 * @param task 要加入队列的任务。
 */
void AsyncCacheInvalidator::enqueueTask(CacheInvalidationTask task) {
    if (!m_running) {
        ::ll::mod::NativeMod::current()->getLogger().warn(
            "AsyncCacheInvalidator: 未运行，任务被丢弃。任务类型: {}, 数据: {}",
            static_cast<int>(task.type),
            task.data
        );
        return;
    }

    bool task_merged = false;
    {
        std::unique_lock<std::mutex> pendingLock(m_pendingTasksMutex); // 锁定 m_pendingTasksMutex
        // 任务合并逻辑
        if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
            if (m_allGroupsModifiedPending || m_pendingGroupModifiedTasks.count(task.data)) {
                ::ll::mod::NativeMod::current()->getLogger().debug(
                    "AsyncCacheInvalidator: 合并 GROUP_MODIFIED 任务，组: '{}'。",
                    task.data
                );
                task_merged = true;
            } else {
                m_pendingGroupModifiedTasks.insert(task.data);
                ::ll::mod::NativeMod::current()->getLogger().debug(
                    "AsyncCacheInvalidator: 入队 GROUP_MODIFIED 任务，组: \'{}\'.",
                    task.data
                );
            }
        } else if (task.type == CacheInvalidationTaskType::PLAYER_GROUP_CHANGED) {
            if (m_pendingPlayerGroupChangedTasks.count(task.data)) {
                ::ll::mod::NativeMod::current()->getLogger().debug(
                    "AsyncCacheInvalidator: 合并 PLAYER_GROUP_CHANGED 任务，玩家: \'{}\'.",
                    task.data
                );
                task_merged = true;
            } else {
                m_pendingPlayerGroupChangedTasks.insert(task.data);
                ::ll::mod::NativeMod::current()->getLogger().debug(
                    "AsyncCacheInvalidator: 入队 PLAYER_GROUP_CHANGED 任务，玩家: \'{}\'.",
                    task.data
                );
            }
        } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
            if (m_allGroupsModifiedPending) {
                ::ll::mod::NativeMod::current()->getLogger().debug(
                    "AsyncCacheInvalidator: 合并 ALL_GROUPS_MODIFIED 任务。"
                );
                task_merged = true;
            } else {
                m_pendingGroupModifiedTasks.clear(); // ALL 任务包含所有特定组修改
                m_allGroupsModifiedPending = true;
                ::ll::mod::NativeMod::current()->getLogger().debug(
                    "AsyncCacheInvalidator: 入队 ALL_GROUPS_MODIFIED 任务。"
                );
            }
        } else {
            ::ll::mod::NativeMod::current()->getLogger().debug(
                "AsyncCacheInvalidator: 入队任务，类型: {}, 数据: \'{}\'.",
                static_cast<int>(task.type),
                task.data
            );
        }
    } // pendingLock 在这里释放

    if (task_merged) {
        return; // 任务已合并，无需入队
    }

    // 在 m_pendingTasksMutex 释放后，再锁定 m_queueMutex 将任务推入队列
    {
        std::unique_lock<std::mutex> queueLock(m_queueMutex); // 锁定 m_queueMutex
        m_taskQueue.push(std::move(task));
    } // queueLock 在这里释放
    m_condition.notify_one();
}

/**
 * @brief 工作线程函数，负责从任务队列中取出并处理任务。
 */
void AsyncCacheInvalidator::processTasks() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("AsyncCacheInvalidator: 工作线程开始处理任务。");
    while (true) {
        CacheInvalidationTask task;
        {
            std::unique_lock<std::mutex> queueLock(m_queueMutex); // 只锁定 m_queueMutex
            m_condition.wait(queueLock, [this] { return !m_taskQueue.empty(); });

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        } // queueLock 在这里释放

        // 优先处理 SHUTDOWN 任务
        if (task.type == CacheInvalidationTaskType::SHUTDOWN) {
            logger.debug("AsyncCacheInvalidator: 工作线程收到 SHUTDOWN 任务并正在退出。");
            break; // 退出循环并终止线程
        }

        // 在处理任务之前，更新待处理任务状态，这部分需要 m_pendingTasksMutex
        {
            std::unique_lock<std::mutex> pendingLock(m_pendingTasksMutex); // 锁定 m_pendingTasksMutex
            if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
                m_pendingGroupModifiedTasks.erase(task.data);
                logger.debug("AsyncCacheInvalidator: 从待处理集合中移除 GROUP_MODIFIED 任务，组: \'{}\'.", task.data);
            } else if (task.type == CacheInvalidationTaskType::PLAYER_GROUP_CHANGED) {
                m_pendingPlayerGroupChangedTasks.erase(task.data);
                logger.debug("AsyncCacheInvalidator: 从待处理集合中移除 PLAYER_GROUP_CHANGED 任务，玩家: \'{}\'.", task.data);
            } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
                m_allGroupsModifiedPending = false;
                logger.debug("AsyncCacheInvalidator: 从待处理集合中移除 ALL_GROUPS_MODIFIED 任务。");
            }
        } // pendingLock 在这里释放

        try {
            switch (task.type) {
            case CacheInvalidationTaskType::GROUP_MODIFIED: {
                logger.debug("AsyncCacheInvalidator: 正在处理 GROUP_MODIFIED 任务，组: \'{}\'.", task.data);
                // 组的权限或继承关系发生变化。
                // 这会影响组本身、其所有子组以及这些组中的所有玩家。
                std::set<std::string> affectedGroups = m_cache.getChildGroupsRecursive(task.data);
                logger
                    .debug("AsyncCacheInvalidator: 组 \'{}\' 及其子组共 {} 个受影响。", task.data, affectedGroups.size());
                for (const auto& groupName : affectedGroups) {
                    m_cache.invalidateGroupPermissions(groupName);
                    logger.debug("AsyncCacheInvalidator: 使组 \'{}\' 的权限缓存失效。", groupName);

                    std::vector<std::string> affectedPlayers = getAffectedPlayersByGroup(groupName);
                    logger.debug(
                        "AsyncCacheInvalidator: 组 \'{}\' 中有 {} 个受影响的玩家。",
                        groupName,
                        affectedPlayers.size()
                    );
                    for (const auto& playerUuid : affectedPlayers) {
                        logger.debug(
                            "AsyncCacheInvalidator: 正在为玩家 \'{}\' 使缓存失效，因为其所在的组 \'{}\' 已被修改。",
                            playerUuid,
                            groupName
                        );
                        m_cache.invalidatePlayerPermissions(playerUuid);
                        m_cache.invalidatePlayerGroups(playerUuid);
                        logger.debug("AsyncCacheInvalidator: 已成功使玩家 \'{}\' 的权限和组缓存失效。", playerUuid);
                    }
                }
                break;
            }
            case CacheInvalidationTaskType::PLAYER_GROUP_CHANGED: {
                logger.debug("AsyncCacheInvalidator: 正在处理 PLAYER_GROUP_CHANGED 任务，玩家: \'{}\'.", task.data);
                // 玩家被添加/从组中移除。
                m_cache.invalidatePlayerPermissions(task.data);
                m_cache.invalidatePlayerGroups(task.data);
                logger.debug("AsyncCacheInvalidator: 使玩家 \'{}\' 的权限和组缓存失效。", task.data);
                break;
            }
            case CacheInvalidationTaskType::ALL_GROUPS_MODIFIED: {
                logger.debug("AsyncCacheInvalidator: 正在处理 ALL_GROUPS_MODIFIED 任务。");
                // 全局性变化，例如新的权限注册。
                m_cache.invalidateAllGroupPermissions();
                m_cache.invalidateAllPlayerPermissions();
                logger.debug("AsyncCacheInvalidator: 使所有组权限和所有玩家权限缓存失效。");
                break;
            }
            case CacheInvalidationTaskType::ALL_PLAYERS_MODIFIED: {
                logger.debug("AsyncCacheInvalidator: 正在处理 ALL_PLAYERS_MODIFIED 任务。");
                // 影响所有玩家的变化，例如默认权限值改变。
                m_cache.invalidateAllPlayerPermissions();
                logger.debug("AsyncCacheInvalidator: 使所有玩家权限缓存失效。");
                break;
            }
            case CacheInvalidationTaskType::SHUTDOWN:
                // 此情况已在循环开始时处理
                break;
            }
        } catch (const std::exception& e) {
            logger.error("AsyncCacheInvalidator: 异步任务处理失败，异常: {}", e.what());
        } catch (...) {
            logger.error("AsyncCacheInvalidator: 异步任务处理失败，发生未知异常。");
        }
    }
    logger.debug("AsyncCacheInvalidator: 工作线程退出。");
}

/**
 * @brief 辅助函数，用于查找受组更改影响的所有玩家。
 *        它查询存储层，因为缓存可能不包含所有玩家数据。
 * @param groupName 组名称。
 * @return 受影响玩家的UUID列表。
 */
std::vector<std::string> AsyncCacheInvalidator::getAffectedPlayersByGroup(const std::string& groupName) {
    auto&                 logger = ::ll::mod::NativeMod::current()->getLogger();
    std::set<std::string> affectedPlayerUuids;

    // 从缓存中获取所有后代组（包括组本身）。
    // 假设继承缓存是可靠的。
    std::set<std::string> allRelatedGroups = m_cache.getChildGroupsRecursive(groupName);

    logger.debug(
        "AsyncCacheInvalidator: 正在查找组 \'{}\' 的受影响玩家。相关组数量: {}",
        groupName,
        allRelatedGroups.size()
    );

    // 对于每个相关组，从存储层获取其成员。
    // 首先，收集所有组名以批量查询其ID。
    std::set<std::string> groupNamesToFetchIds;
    for (const auto& gName : allRelatedGroups) {
        groupNamesToFetchIds.insert(gName);
    }

    // 批量获取组ID
    std::unordered_map<std::string, std::string> groupNameToIdMap =
        m_storage.fetchGroupIdsByNames(groupNamesToFetchIds);

    std::vector<std::string> groupIdsToFetchPlayers;
    for (const auto& gName : allRelatedGroups) {
        auto it = groupNameToIdMap.find(gName);
        if (it != groupNameToIdMap.end()) {
            groupIdsToFetchPlayers.push_back(it->second);
        }
    }

    // 批量获取这些组中的玩家
    if (!groupIdsToFetchPlayers.empty()) {
        std::vector<std::string> playersInGroups = m_storage.fetchPlayersInGroups(groupIdsToFetchPlayers);
        affectedPlayerUuids.insert(playersInGroups.begin(), playersInGroups.end());
        logger.debug(
            "AsyncCacheInvalidator: 从 {} 个组中获取到 {} 个玩家。",
            groupIdsToFetchPlayers.size(),
            playersInGroups.size()
        );
    } else {
        logger.debug("AsyncCacheInvalidator: 没有需要获取玩家的组ID。");
    }

    return std::vector<std::string>(affectedPlayerUuids.begin(), affectedPlayerUuids.end());
}


} // namespace internal
} // namespace permission
} // namespace BA
