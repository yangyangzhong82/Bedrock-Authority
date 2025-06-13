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
#include <unordered_map> // For cache
#include <shared_mutex>  // For thread-safe cache access
#include <set>           // For internal use in getAllPermissionsForPlayer

namespace BA { namespace db { class IDatabase; } } // 前向声明 IDatabase

namespace BA {
namespace permission {

// 新增结构体用于返回组的详细信息
struct BA_API GroupDetails {
    std::string id;
    std::string name;
    std::string description;
    int priority;
    bool isValid = false; // 指示此结构体是否包含有效数据

    // 默认构造函数
    GroupDetails() : id(""), name(""), description(""), priority(0), isValid(false) {}

    // 带参数的构造函数
    GroupDetails(std::string id, std::string name, std::string description, int priority)
        : id(std::move(id)), name(std::move(name)), description(std::move(description)), priority(priority), isValid(true) {}
};


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

    /// 获取组的详细信息（名称、描述、优先级）。如果组不存在，返回 isValid 为 false 的 GroupDetails。
    GroupDetails getGroupDetails(const std::string& groupName);

    /// 更新组的描述。
    bool updateGroupDescription(const std::string& groupName, const std::string& newDescription);

    /// 获取组的描述。如果组不存在，返回空字符串。
    std::string getGroupDescription(const std::string& groupName);

private:
    PermissionManager() = default;
    void ensureTables(); // 确保表存在

    // 辅助函数：根据名称从权限表或组表中获取 ID
    std::string getIdByName(const std::string& table, const std::string& name);

    // 辅助函数：将通配符模式转换为正则表达式字符串
    std::string wildcardToRegex(const std::string& pattern);

    // 新增辅助函数：递归获取所有子组
    std::set<std::string> getChildGroupsRecursive(const std::string& groupName);
    // 新增辅助函数：获取受特定组修改影响的所有玩家 UUID
    std::vector<std::string> getAffectedPlayersByGroup(const std::string& groupName);

    // --- 缓存相关 ---
    // 从缓存或数据库获取组 ID
    std::string getCachedGroupId(const std::string& groupName);
    // 使特定组的缓存失效
    void invalidateGroupCache(const std::string& groupName);
    // 更新或插入组到缓存
    void updateGroupCache(const std::string& groupName, const std::string& groupId);
    // 从数据库加载所有组到缓存（可选，可在 init 时调用）
    void populateGroupCache();
    // 使特定玩家的权限缓存失效
    void invalidatePlayerPermissionsCache(const std::string& playerUuid);
    // 使所有玩家的权限缓存失效
    void invalidateAllPlayerPermissionsCache();
    // 使特定组的权限缓存失效
    void invalidateGroupPermissionsCache(const std::string& groupName);
    // 使所有组的权限缓存失效
    void invalidateAllGroupPermissionsCache();
    // 从数据库加载所有组的权限到缓存
    void populateGroupPermissionsCache();
    // --- 结束缓存相关 ---


    db::IDatabase* db_ = nullptr;
    // 组名到组 ID 的缓存
    std::unordered_map<std::string, std::string> groupNameCache_;
    // 用于保护组名缓存访问的读写锁
    mutable std::shared_mutex cacheMutex_; // mutable 允许在 const 方法中锁定

    // 玩家权限缓存
    std::unordered_map<std::string, std::vector<std::string>> playerPermissionsCache_;
    // 用于保护玩家权限缓存的读写锁
    mutable std::shared_mutex playerPermissionsCacheMutex_;

    // 组权限缓存 (新添加)
    std::unordered_map<std::string, std::vector<std::string>> groupPermissionsCache_;
    // 用于保护组权限缓存的读写锁 (新添加)
    mutable std::shared_mutex groupPermissionsCacheMutex_;
}; 

} // namespace permission
} // namespace BA
