#include "mod/MyMod.h"

#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/Config.h"
#include "db/DatabaseFactory.h"
#include <exception>
#include "RemoteCallAPI.h"
#include "command/Command.h"
#include "permission/PermissionManager.h" // 添加 PermissionManager 头文件
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

        // 初始化 PermissionManager
        if (db_) {
            permission::PermissionManager::getInstance().init(db_.get()); // 使用 get() 获取原始指针
            getSelf().getLogger().info("PermissionManager initialized with the database connection.");
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
    getSelf().getLogger().info("Mod enabled");
    BA::Command::RegisterCommands();

    // Export PermissionManager functions using RemoteCall
    auto& pm = permission::PermissionManager::getInstance();
    const std::string ns = "BA"; // Namespace for exported functions

    RemoteCall::exportAs(ns, "registerPermission", [&](const std::string& name, const std::string& description, bool defaultValue) {
        return pm.registerPermission(name, description, defaultValue);
    });
    RemoteCall::exportAs(ns, "permissionExists", [&](const std::string& name) {
        return pm.permissionExists(name);
    });
    RemoteCall::exportAs(ns, "getAllPermissions", [&]() {
        return pm.getAllPermissions();
    });
    RemoteCall::exportAs(ns, "createGroup", [&](const std::string& groupName, const std::string& description) {
        return pm.createGroup(groupName, description);
    });
    RemoteCall::exportAs(ns, "groupExists", [&](const std::string& groupName) {
        return pm.groupExists(groupName);
    });
    RemoteCall::exportAs(ns, "getAllGroups", [&]() {
        return pm.getAllGroups();
    });
    RemoteCall::exportAs(ns, "deleteGroup", [&](const std::string& groupName) {
        return pm.deleteGroup(groupName);
    });
    RemoteCall::exportAs(ns, "addPermissionToGroup", [&](const std::string& groupName, const std::string& permissionName) {
        return pm.addPermissionToGroup(groupName, permissionName);
    });
    RemoteCall::exportAs(ns, "removePermissionFromGroup", [&](const std::string& groupName, const std::string& permissionName) {
        return pm.removePermissionFromGroup(groupName, permissionName);
    });
    RemoteCall::exportAs(ns, "getDirectPermissionsOfGroup", [&](const std::string& groupName) {
        return pm.getDirectPermissionsOfGroup(groupName);
    });
    RemoteCall::exportAs(ns, "getPermissionsOfGroup", [&](const std::string& groupName) {
        return pm.getPermissionsOfGroup(groupName);
    });
    RemoteCall::exportAs(ns, "addGroupInheritance", [&](const std::string& groupName, const std::string& parentGroupName) {
        return pm.addGroupInheritance(groupName, parentGroupName);
    });
    RemoteCall::exportAs(ns, "removeGroupInheritance", [&](const std::string& groupName, const std::string& parentGroupName) {
        return pm.removeGroupInheritance(groupName, parentGroupName);
    });
    RemoteCall::exportAs(ns, "getParentGroups", [&](const std::string& groupName) {
        return pm.getParentGroups(groupName);
    });
    RemoteCall::exportAs(ns, "addPlayerToGroup", [&](const std::string& playerUuid, const std::string& groupName) {
        return pm.addPlayerToGroup(playerUuid, groupName);
    });
    RemoteCall::exportAs(ns, "removePlayerFromGroup", [&](const std::string& playerUuid, const std::string& groupName) {
        return pm.removePlayerFromGroup(playerUuid, groupName);
    });
    RemoteCall::exportAs(ns, "getPlayerGroups", [&](const std::string& playerUuid) {
        return pm.getPlayerGroups(playerUuid);
    });
    RemoteCall::exportAs(ns, "getPlayerGroupIds", [&](const std::string& playerUuid) {
        return pm.getPlayerGroupIds(playerUuid);
    });
    RemoteCall::exportAs(ns, "getPlayersInGroup", [&](const std::string& groupName) {
        return pm.getPlayersInGroup(groupName);
    });
    RemoteCall::exportAs(ns, "getAllPermissionsForPlayer", [&](const std::string& playerUuid) {
        return pm.getAllPermissionsForPlayer(playerUuid);
    });
    RemoteCall::exportAs(ns, "setGroupPriority", [&](const std::string& groupName, int priority) {
        return pm.setGroupPriority(groupName, priority);
    });
    RemoteCall::exportAs(ns, "getGroupPriority", [&](const std::string& groupName) {
        return pm.getGroupPriority(groupName);
    });
    RemoteCall::exportAs(ns, "hasPermission", [&](const std::string& playerUuid, const std::string& permissionNode) {
        return pm.hasPermission(playerUuid, permissionNode);
    });

    getSelf().getLogger().info("Exported PermissionManager functions to RemoteCall namespace '{}'", ns);

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
