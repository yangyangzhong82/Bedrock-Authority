#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace BA {
namespace permission {

class PermissionManager; // 前向声明 PermissionManager

/**
 * @brief CleanupScheduler 类负责定期触发权限管理器的清理任务。
 *        它使用一个独立的线程来执行周期性操作，避免阻塞主线程。
 */
class CleanupScheduler {
public:
    /**
     * @brief 构造函数。
     * @param intervalSeconds 清理任务的执行间隔（秒）。
     */
    explicit CleanupScheduler(long long intervalSeconds);

    /**
     * @brief 析构函数。确保在对象销毁时停止所有工作线程。
     */
    ~CleanupScheduler();

    /**
     * @brief 启动清理调度器。
     */
    void start();

    /**
     * @brief 停止清理调度器，并等待其线程完成。
     */
    void stop();

private:
    /**
     * @brief 工作线程函数，负责周期性地调用清理任务。
     */
    void run();

    std::atomic<bool> m_running;           /**< 指示调度器是否正在运行的原子标志 */
    std::thread       m_workerThread;      /**< 执行清理任务的工作线程 */
    std::mutex        m_mutex;             /**< 保护条件变量的互斥锁 */
    std::condition_variable m_condition;   /**< 用于通知工作线程停止的条件变量 */
    long long         m_intervalSeconds;   /**< 清理任务的执行间隔（秒） */
};

} // namespace permission
} // namespace BA
