#pragma once

// Define BA_API based on whether we are building the DLL or using it
#ifdef BA_EXPORTS // This should be defined by the build system when building the DLL
    #ifdef _WIN32
        #define BA_API __declspec(dllexport)
    #else
        #define BA_API __attribute__((visibility("default"))) // For GCC/Clang on Linux/macOS
    #endif
#else // We are using the DLL
    #ifdef _WIN32
        #define BA_API __declspec(dllimport)
    #else
        #define BA_API // Import is default visibility on GCC/Clang
    #endif
#endif

#include <string>
#include <vector>
#include "db/IDatabase.h"

namespace BA {
namespace permission {

class BA_API PermissionManager { // Export the class itself if needed, or just members
public:
    static PermissionManager& getInstance(); // Static instance getter might not need export depending on usage

    /// 使用数据库实例初始化管理器（必须在调用其他 API 之前调用）。
    void init(db::IDatabase* db);

    /// 注册一个新权限。如果已存在或出错，则返回 false。
    bool registerPermission(const std::string& name, const std::string& description = "", bool defaultValue = false);
    /// 检查权限是否存在
    bool permissionExists(const std::string& name);
    /// 获取所有权限名称
    std::vector<std::string> getAllPermissions();

    /// 创建一个权限组。如果已存在或出错，则返回 false。
    bool createGroup(const std::string& groupName, const std::string& description = "");
    /// 检查组是否存在
    bool groupExists(const std::string& groupName);
    /// 获取所有组名称
    std::vector<std::string> getAllGroups();
    /// 删除一个权限组。如果不存在或出错，则返回 false。
    bool deleteGroup(const std::string& groupName);

    /// 将权限添加到组。如果组或权限不存在，或已分配，则返回 false。
    bool addPermissionToGroup(const std::string& groupName, const std::string& permissionName);
    /// 从组中移除权限
    bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionName);
    /// 获取组直接拥有的权限（不包括继承的）
    std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName);
    /// 获取组的最终权限（包括继承的）
    std::vector<std::string> getPermissionsOfGroup(const std::string& groupName);

    /// 添加继承：子组继承父组。如果无效或已设置，则返回 false。
    bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /// 移除继承
    bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /// 获取直接父组
    std::vector<std::string> getParentGroups(const std::string& groupName);

    /// 将玩家分配到权限组
    bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName);
    /// 从权限组中移除玩家
    bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName);
    /// 获取玩家所属的组名称
    std::vector<std::string> getPlayerGroups(const std::string& playerUuid);
    /// 获取玩家所属的组 ID (字符串形式)
    std::vector<std::string> getPlayerGroupIds(const std::string& playerUuid);
    /// 获取权限组中的玩家
    std::vector<std::string> getPlayersInGroup(const std::string& groupName);
    /// 获取玩家最终生效的所有权限规则（考虑优先级、继承和否定）
    std::vector<std::string> getAllPermissionsForPlayer(const std::string& playerUuid);

    /// 设置权限组的优先级（优先级越高越优先）
    bool setGroupPriority(const std::string& groupName, int priority);
    /// 获取权限组的优先级
    int getGroupPriority(const std::string& groupName);
    /// 检查玩家是否拥有特定权限（支持通配符和否定）
    bool hasPermission(const std::string& playerUuid, const std::string& permissionNode);

private:
    PermissionManager() = default;
    void ensureTables(); // 确保表存在
    // 辅助函数：根据名称从权限表或组表中获取 ID
    std::string getIdByName(const std::string& table, const std::string& name);

    db::IDatabase* db_ = nullptr;
};

} // namespace permission
} // namespace BA
