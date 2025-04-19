#include "mod/MyMod.h"

#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/Config.h"
#include "db/DatabaseFactory.h"
#include <exception>

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
        } else {
            db_ = db::DatabaseFactory::createMySQL(config_.mysql_host,
                                                   config_.mysql_user,
                                                   config_.mysql_password,
                                                   config_.mysql_db,
                                                   config_.mysql_port);
        }
        getSelf().getLogger().info("Database '%s' initialized", config_.db_type.c_str());
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
    getSelf().getLogger().info("Mod enabled");
    return true;
}

bool MyMod::disable() {
    getSelf().getLogger().info("Disabling mod...");
    // Code for disabling the mod goes here.
    getSelf().getLogger().info("Mod disabled");
    return true;
}

} // namespace my_mod

LL_REGISTER_MOD(BA::MyMod, BA::MyMod::getInstance());
