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

class PermissionCache;   // 前向声明 PermissionCache 类
class PermissionStorage; // 前向声明 PermissionStorage 类

/**
 * @brief 异步缓存失效器，负责在后台线程中处理缓存失效任务。
 *        它通过任务队列和线程池机制，异步地更新权限缓存，以避免阻塞主线程。
 */
class AsyncCacheInvalidator {
public:
    /**
     * @brief 构造函数。
     * @param cache 权限缓存的引用。
     * @param storage 权限存储的引用。
     */
    AsyncCacheInvalidator(PermissionCache& cache, PermissionStorage& storage);
    /**
     * @brief 析构函数。确保在对象销毁时停止所有工作线程。
     */
    ~AsyncCacheInvalidator();

    /**
     * @brief 启动异步缓存失效器。
     * @param threadPoolSize 线程池中工作线程的数量。
     */
    void start(unsigned int threadPoolSize);
    /**
     * @brief 停止异步缓存失效器，并等待所有工作线程完成。
     */
    void stop();
    /**
     * @brief 将一个缓存失效任务加入队列。
     * @param task 要加入队列的任务。
     */
    void enqueueTask(CacheInvalidationTask task);

private:
    /**
     * @brief 工作线程函数，负责从任务队列中取出并处理任务。
     */
    void                     processTasks();
    /**
     * @brief 根据组名获取受影响的玩家UUID列表。
     *        此函数会查询存储层以获取玩家数据。
     * @param groupName 组名称。
     * @return 受影响玩家的UUID列表。
     */
    std::vector<std::string> getAffectedPlayersByGroup(const std::string& groupName);

    PermissionCache&   m_cache;         /**< 权限缓存的引用 */
    PermissionStorage& m_storage;       /**< 权限存储的引用 */

    std::queue<CacheInvalidationTask> m_taskQueue;     /**< 待处理任务队列 */
    std::mutex                        m_queueMutex;    /**< 保护任务队列的互斥锁 */
    std::condition_variable           m_condition;     /**< 用于通知工作线程有新任务的条件变量 */
    std::vector<std::thread>          m_workerThreads; /**< 工作线程池 */
    std::atomic<bool>                 m_running = false; /**< 指示失效器是否正在运行的原子标志 */

    // 任务合并成员
    std::mutex            m_pendingTasksMutex;      /**< 保护待处理任务集合的互斥锁 */
    std::set<std::string> m_pendingGroupModifiedTasks; /**< 待处理的组修改任务集合，用于合并 */
    std::set<std::string> m_pendingPlayerGroupChangedTasks; /**< 待处理的玩家组改变任务集合，用于合并 */
    bool                  m_allGroupsModifiedPending = false; /**< 标记是否有 ALL_GROUPS_MODIFIED 任务待处理 */
};

} // namespace internal
} // namespace permission
} // namespace BA
