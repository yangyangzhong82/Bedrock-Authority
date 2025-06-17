#include "permission/CleanupScheduler.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include "permission/PermissionManager.h" // 包含 PermissionManager 头文件

namespace BA {
namespace permission {

/**
 * @brief 构造函数。
 * @param intervalSeconds 清理任务的执行间隔（秒）。
 */
CleanupScheduler::CleanupScheduler(long long intervalSeconds)
: m_running(false),
  m_intervalSeconds(intervalSeconds) {
    ::ll::mod::NativeMod::current()->getLogger().debug("CleanupScheduler: 构造函数被调用，间隔 {} 秒。", intervalSeconds);
}

/**
 * @brief 析构函数。确保在对象销毁时停止所有工作线程。
 */
CleanupScheduler::~CleanupScheduler() {
    if (m_running) {
        stop();
    }
    ::ll::mod::NativeMod::current()->getLogger().debug("CleanupScheduler: 析构函数被调用。");
}

/**
 * @brief 启动清理调度器。
 */
void CleanupScheduler::start() {
    if (m_running) {
        ::ll::mod::NativeMod::current()->getLogger().warn("CleanupScheduler: 已经运行，无需再次启动。");
        return;
    }
    m_running = true;
    m_workerThread = std::thread(&CleanupScheduler::run, this);
    ::ll::mod::NativeMod::current()->getLogger().info("CleanupScheduler: 已启动，清理间隔为 {} 秒。", m_intervalSeconds);
}

/**
 * @brief 停止清理调度器，并等待其线程完成。
 */
void CleanupScheduler::stop() {
    if (!m_running) {
        ::ll::mod::NativeMod::current()->getLogger().warn("CleanupScheduler: 已经停止，无需再次停止。");
        return;
    }

    m_running = false; // 设置停止标志
    m_condition.notify_one(); // 唤醒等待中的线程

    if (m_workerThread.joinable()) {
        m_workerThread.join(); // 等待线程完成
        ::ll::mod::NativeMod::current()->getLogger().debug("CleanupScheduler: 工作线程已加入。");
    }
    ::ll::mod::NativeMod::current()->getLogger().info("CleanupScheduler: 已停止。");
}

/**
 * @brief 工作线程函数，负责周期性地调用清理任务。
 */
void CleanupScheduler::run() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("CleanupScheduler: 工作线程开始运行。");

    while (m_running) {
        try {
            // 执行清理任务
            PermissionManager::getInstance().runPeriodicCleanup();
            logger.debug("CleanupScheduler: 已执行一次定期清理任务。");
        } catch (const std::exception& e) {
            logger.error("CleanupScheduler: 执行清理任务失败，异常: {}", e.what());
        } catch (...) {
            logger.error("CleanupScheduler: 执行清理任务失败，发生未知异常。");
        }

        // 等待下一个周期，或者被停止信号唤醒
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait_for(lock, std::chrono::seconds(m_intervalSeconds), [this] { return !m_running; });
    }
    logger.debug("CleanupScheduler: 工作线程退出。");
}

} // namespace permission
} // namespace BA
