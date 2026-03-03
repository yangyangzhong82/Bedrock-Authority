#include "mod/MyMod.h"

#include "command/Command.h"
#include "db/DatabaseFactory.h"
#include "ll/api/Config.h"
#include "ll/api/mod/RegisterHelper.h"
#include "permission/PermissionManager.h" // 添加 PermissionManager 头文件
#include <exception>
#include "permission/events/EventTest.h" // Include EventTest.h
namespace BA {

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    getSelf().getLogger().debug("Loading mod...");
    // load config
    auto configPath  = getSelf().getConfigDir();
    configPath      /= "config.json";
    configPath.make_preferred();
    // initialize default config
    bool noRewrite = ll::config::loadConfig(config_, configPath);
    getSelf().getLogger().info(
        "Config loaded: type={}, version={} (rewrite: {})",
        config_.db_type,
        config_.version,
        noRewrite ? "false" : "true"
    );
    // create database
    try {
        if (config_.db_type == "sqlite") {
            db_ = db::DatabaseFactory::createSQLite(config_.sqlite_path);
        } else if (config_.db_type == "mysql") {
            db_ = db::DatabaseFactory::createMySQL(
                config_.mysql_host,
                config_.mysql_user,
                config_.mysql_password,
                config_.mysql_db,
                config_.mysql_port
            );
        } else if (config_.db_type == "postgresql") {
            db_ = db::DatabaseFactory::createPostgreSQL(
                config_.postgresql_host,
                config_.postgresql_user,
                config_.postgresql_password,
                config_.postgresql_db,
                config_.postgresql_port
            );
        }

        getSelf().getLogger().info("Database '{}' initialized", config_.db_type);

        // 初始化 PermissionManager
        if (db_) {
            if (!permission::PermissionManager::getInstance().init(
                    db_.get(),
                    config_.enable_cache_warmup,
                    config_.cache_worker_threads
                )) { // 使用 get() 获取原始指针并传递预热配置和线程池大小
                getSelf().getLogger().error("PermissionManager 初始化失败，无法继续加载。");
                return false; // 阻止加载
            }
            getSelf().getLogger().info("PermissionManager initialized with the database connection.");

            // 初始化 CleanupScheduler
            m_cleanupScheduler = std::make_unique<permission::CleanupScheduler>(config_.cleanup_interval_seconds);
            getSelf().getLogger().info("CleanupScheduler initialized with interval {} seconds.", config_.cleanup_interval_seconds);

        } else {
            // 这个分支理论上不应该执行，因为如果 db_ 为空，上面的 catch 会捕获异常
            getSelf().getLogger().error(
                "Database pointer is null after creation attempt, cannot initialize PermissionManager."
            );            // 使用 error 级别
            return false; // 阻止加载
        }

    } catch (const std::exception& e) {
        getSelf().getLogger().error("Error initializing database '{}': {}", config_.db_type, e.what());
        return false;
    }
    getSelf().getLogger().info("Load sequence complete");
    // Code for loading the mod goes here.
    return true;
}

bool MyMod::enable() {
    getSelf().getLogger().info("Enabling mod...");
    // Code for enabling the mod goes here.
 
    if (m_cleanupScheduler) {
        m_cleanupScheduler->start();
    }
    getSelf().getLogger().info("Mod enabled");
    BA::Command::RegisterCommands();

    // Register and trigger test events
    BA::permission::event::registerTestListeners();
    BA::permission::event::triggerTestEvents();


    return true;
}

bool MyMod::disable() {
    getSelf().getLogger().info("Disabling mod...");

    // 停止清理调度器
    if (m_cleanupScheduler) {
        m_cleanupScheduler->stop();
    }
    // 关闭 PermissionManager，停止其异步工作线程
    permission::PermissionManager::getInstance().shutdown();
    getSelf().getLogger().info("Mod disabled");
    return true;
}

} // namespace BA

LL_REGISTER_MOD(BA::MyMod, BA::MyMod::getInstance());
