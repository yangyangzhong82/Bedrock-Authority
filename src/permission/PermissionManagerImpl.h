#pragma once

#include "permission/PermissionData.h"
#include "permission/PermissionManager.h" // 包含主类的定义以访问其作用域
#include <memory>
#include <string>
#include <vector>

// 前向声明内部类，以避免在头文件中包含它们的完整定义，减少编译依赖
namespace BA {
namespace db {
class IDatabase;
}
namespace permission {
namespace internal {
class PermissionStorage;
class PermissionCache;
class AsyncCacheInvalidator;
} // namespace internal
} // namespace permission
} // namespace BA


namespace BA {
namespace permission {

// 这是 PermissionManager 的 PIMPL 实现类。
// 它位于 PermissionManager 类的作用域内，但定义在一个单独的头文件中。
class PermissionManager::PermissionManagerImpl {
public:
    // 构造和析构函数
    PermissionManagerImpl();
    ~PermissionManagerImpl();

    // --- 核心生命周期 ---
    bool init(db::IDatabase* db, bool enableWarmup, unsigned int threadPoolSize);
    void shutdown();

    // --- 权限管理 ---
    bool registerPermission(const std::string& name, const std::string& description, bool defaultValue);
    bool permissionExists(const std::string& name);
    std::vector<std::string> getAllPermissions();

    // --- 组管理 ---
    bool                     createGroup(const std::string& groupName, const std::string& description);
    bool                     deleteGroup(const std::string& groupName);
    bool                     groupExists(const std::string& groupName);
    std::vector<std::string> getAllGroups();
    GroupDetails             getGroupDetails(const std::string& groupName);
    bool                     updateGroupDescription(const std::string& groupName, const std::string& newDescription);
    std::string              getGroupDescription(const std::string& groupName);

    // --- 组权限 ---
    bool                     addPermissionToGroup(const std::string& groupName, const std::string& permissionRule);
    bool                     removePermissionFromGroup(const std::string& groupName, const std::string& permissionRule);
    std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName);
    std::vector<CompiledPermissionRule> getPermissionsOfGroup(const std::string& groupName);
    size_t addPermissionsToGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);
    size_t removePermissionsFromGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);

    // --- 组继承 ---
    bool                     addGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    bool                     removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    std::vector<std::string> getAllAncestorGroups(const std::string& groupName);
    std::vector<std::string> getDirectParentGroups(const std::string& groupName);

    // --- 组优先级 ---
    bool setGroupPriority(const std::string& groupName, int priority);
    int  getGroupPriority(const std::string& groupName);

    // --- 玩家管理 ---
    bool                      addPlayerToGroup(const std::string& playerUuid, const std::string& groupName);
    bool                      removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName);
    std::vector<std::string>  getPlayerGroups(const std::string& playerUuid);
    std::vector<std::string>  getPlayerGroupIds(const std::string& playerUuid);
    std::vector<std::string>  getPlayersInGroup(const std::string& groupName);
    std::vector<GroupDetails> getPlayerGroupsWithPriorities(const std::string& playerUuid);
    size_t addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);
    size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);

    // --- 权限检查 ---
    std::vector<CompiledPermissionRule> getAllPermissionsForPlayer(const std::string& playerUuid);
    bool                                hasPermission(const std::string& playerUuid, const std::string& permissionNode);

private:
    // --- 辅助方法 ---
    std::string getCachedGroupId(const std::string& groupName);
    std::regex  wildcardToRegex(const std::string& pattern);
    void        populateAllCaches();

    // --- 成员变量 ---
    // 使用 unique_ptr 来管理内部组件的生命周期
    std::unique_ptr<internal::PermissionStorage>     m_storage;
    std::unique_ptr<internal::PermissionCache>       m_cache;
    std::unique_ptr<internal::AsyncCacheInvalidator> m_invalidator;

    bool m_initialized = false;
};

} // namespace permission
} // namespace BA
