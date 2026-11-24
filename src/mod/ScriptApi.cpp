#include "mod/ScriptApi.h"

#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include "permission/PermissionManager.h"

#ifdef BA_WITH_LEGACY_REMOTE_CALL
#include <Windows.h>
#include <RemoteCallAPI.h>
#endif

namespace BA {

void registerScriptApi() {
#ifdef BA_WITH_LEGACY_REMOTE_CALL
    if (!GetModuleHandleW(L"LegacyRemoteCall.dll")) {
        ll::mod::NativeMod::current()->getLogger().debug("LegacyRemoteCall 未加载，跳过 JS API 导出。");
        return;
    }

    using permission::PermissionManager;
    auto& pm = PermissionManager::getInstance();

    RemoteCall::exportAs("BA", "registerPermission", [&pm](std::string name, std::string description, bool defaultValue) {
        return pm.registerPermission(name, description, defaultValue);
    });
    RemoteCall::exportAs("BA", "permissionExists", [&pm](std::string name) { return pm.permissionExists(name); });
    RemoteCall::exportAs("BA", "getAllPermissions", [&pm]() { return pm.getAllPermissions(); });
    RemoteCall::exportAs("BA", "createGroup", [&pm](std::string name, std::string description) {
        return pm.createGroup(name, description);
    });
    RemoteCall::exportAs("BA", "groupExists", [&pm](std::string name) { return pm.groupExists(name); });
    RemoteCall::exportAs("BA", "getAllGroups", [&pm]() { return pm.getAllGroups(); });
    RemoteCall::exportAs("BA", "deleteGroup", [&pm](std::string name) { return pm.deleteGroup(name); });
    RemoteCall::exportAs("BA", "addPermissionToGroup", [&pm](std::string group, std::string permission) {
        return pm.addPermissionToGroup(group, permission);
    });
    RemoteCall::exportAs("BA", "removePermissionFromGroup", [&pm](std::string group, std::string permission) {
        return pm.removePermissionFromGroup(group, permission);
    });
    RemoteCall::exportAs("BA", "getDirectPermissionsOfGroup", [&pm](std::string group) {
        return pm.getDirectPermissionsOfGroup(group);
    });
    RemoteCall::exportAs("BA", "getPermissionsOfGroup", [&pm](std::string group) {
        std::vector<std::string> patterns;
        for (const auto& rule : pm.getPermissionsOfGroup(group)) patterns.push_back(rule.pattern);
        return patterns;
    });
    RemoteCall::exportAs("BA", "addGroupInheritance", [&pm](std::string group, std::string parent) {
        return pm.addGroupInheritance(group, parent);
    });
    RemoteCall::exportAs("BA", "removeGroupInheritance", [&pm](std::string group, std::string parent) {
        return pm.removeGroupInheritance(group, parent);
    });
    RemoteCall::exportAs("BA", "getParentGroups", [&pm](std::string group) {
        return pm.getDirectParentGroups(group);
    });
    RemoteCall::exportAs("BA", "setGroupPriority", [&pm](std::string group, int priority) {
        return pm.setGroupPriority(group, priority);
    });
    RemoteCall::exportAs("BA", "getGroupPriority", [&pm](std::string group) { return pm.getGroupPriority(group); });
    RemoteCall::exportAs("BA", "addPlayerToGroup", [&pm](std::string uuid, std::string group) {
        return pm.addPlayerToGroup(uuid, group);
    });
    RemoteCall::exportAs("BA", "removePlayerFromGroup", [&pm](std::string uuid, std::string group) {
        return pm.removePlayerFromGroup(uuid, group);
    });
    RemoteCall::exportAs("BA", "getPlayerGroups", [&pm](std::string uuid) { return pm.getPlayerGroups(uuid); });
    RemoteCall::exportAs("BA", "getPlayerGroupIds", [&pm](std::string uuid) {
        return pm.getPlayerGroupIds(uuid);
    });
    RemoteCall::exportAs("BA", "getPlayersInGroup", [&pm](std::string group) {
        return pm.getPlayersInGroup(group);
    });
    RemoteCall::exportAs("BA", "getAllPermissionsForPlayer", [&pm](std::string uuid) {
        std::vector<std::string> patterns;
        for (const auto& rule : pm.getAllPermissionsForPlayer(uuid)) patterns.push_back(rule.pattern);
        return patterns;
    });
    RemoteCall::exportAs("BA", "hasPermission", [&pm](std::string uuid, std::string permission) {
        return pm.hasPermission(uuid, permission);
    });

    ll::mod::NativeMod::current()->getLogger().info("已导出 Bedrock Authority JS API。");
#endif
}

} // namespace BA
