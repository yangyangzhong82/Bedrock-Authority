#include "mod/MyMod.h"

#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/Config.h"
#include "db/DatabaseFactory.h"
#include <exception>
#include "RemoteCallAPI.h"
#include "command/Command.h"
#include "permission/PermissionManager.h" // 添加 PermissionManager 头文件
#include "http/HttpServer.h" // 添加 HttpServer 头文件
#include <drogon/drogon.h> // 添加 Drogon 头文件
namespace BA {

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    getSelf().getLogger().debug("Loading mod...");
    // load config
    auto configPath = getSelf().getConfigDir();
    configPath /= "config.json";
    configPath.make_preferred();
    // initialize default config
    bool noRewrite = ll::config::loadConfig(config_, configPath);
    getSelf().getLogger().info("Config loaded: type=%s, version=%d (rewrite: %s)",
        config_.db_type.c_str(), config_.version,
        noRewrite ? "false" : "true");
    // create database
    try {
if (config_.db_type == "sqlite") {
    db_ = db::DatabaseFactory::createSQLite(config_.sqlite_path);
} else if (config_.db_type == "mysql") {
    db_ = db::DatabaseFactory::createMySQL(config_.mysql_host,
                                           config_.mysql_user,
                                           config_.mysql_password,
                                           config_.mysql_db,
                                           config_.mysql_port);
} else if (config_.db_type == "postgresql") {
   /* db_ = db::DatabaseFactory::createPostgreSQL(config_.postgresql_host,
                                                config_.postgresql_user,
                                                config_.postgresql_password,
                                                config_.postgresql_db,
                                                config_.postgresql_port);
                                                */
}

        getSelf().getLogger().info("Database '%s' initialized", config_.db_type.c_str());

        // 初始化 PermissionManager
        if (db_) {
            permission::PermissionManager::getInstance().init(db_.get()); // 使用 get() 获取原始指针
            getSelf().getLogger().info("PermissionManager initialized with the database connection.");

            // 初始化 HttpServer
            httpServer_ = std::make_unique<http::HttpServer>(getSelf(), config_, permission::PermissionManager::getInstance());
            getSelf().getLogger().info("HttpServer initialized.");
      
         } else {
              // 这个分支理论上不应该执行，因为如果 db_ 为空，上面的 catch 会捕获异常
              getSelf().getLogger().error("Database pointer is null after creation attempt, cannot initialize PermissionManager."); // 使用 error 级别
              return false; // 阻止加载
         }

    } catch (const std::exception& e) {
        getSelf().getLogger().error("Error initializing database '%s': %s",
            config_.db_type.c_str(), e.what());
        return false;
    }
    getSelf().getLogger().info("Load sequence complete");
    // Code for loading the mod goes here.
    return true;
}

bool MyMod::enable() {
    getSelf().getLogger().info("Enabling mod...");
    // Code for enabling the mod goes here.
    if (httpServer_) {
        httpServer_->start();
    }
    getSelf().getLogger().info("Mod enabled");
    BA::Command::RegisterCommands();



    return true;
}

bool MyMod::disable() {
    getSelf().getLogger().info("Disabling mod...");
    // 停止 HTTP 服务器
    if (httpServer_) {
        httpServer_->stop();
    }
    getSelf().getLogger().info("Mod disabled");
    return true;
}

} // namespace my_mod

LL_REGISTER_MOD(BA::MyMod, BA::MyMod::getInstance());
