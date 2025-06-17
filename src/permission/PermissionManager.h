#pragma once

#include "permission/PermissionData.h" // 包含公共数据结构
#include <memory>                      // 包含智能指针

namespace BA {
namespace db {
class IDatabase; // 前向声明数据库接口
} // namespace db

namespace permission {

/**
 * @brief PermissionManager 类是唯一的公共接口类。
 *        它充当外观模式，并使用 PIMPL (Pointer to IMPLementation) 惯用法
 *        来隐藏所有实现细节，从而实现接口与实现的分离。
 */
class PermissionManager {
public:
    /**
     * @brief 获取 PermissionManager 的单例实例。
     * @return PermissionManager 的单例引用。
     */
    BA_API static PermissionManager& getInstance();

    // --- 核心生命周期管理 ---
    /**
     * @brief 初始化权限管理器。
     * @param db 数据库接口指针。
     * @param enableWarmup 是否启用预热缓存（默认为 true）。
     * @param threadPoolSize 线程池大小（默认为 4）。
     * @return 如果初始化成功则返回 true，否则返回 false。
     */
    BA_API bool init(db::IDatabase* db, bool enableWarmup = true, unsigned int threadPoolSize = 4);
    /**
     * @brief 关闭权限管理器，释放资源。
     */
    BA_API void shutdown();

    // --- 权限管理 ---
    /**
     * @brief 注册一个权限。
     * @param name 权限名称。
     * @param description 权限描述（默认为空）。
     * @param defaultValue 权限的默认值（默认为 false）。
     * @return 如果注册成功则返回 true，否则返回 false。
     */
    BA_API bool
    registerPermission(const std::string& name, const std::string& description = "", bool defaultValue = false);
    /**
     * @brief 检查权限是否存在。
     * @param name 权限名称。
     * @return 如果权限存在则返回 true，否则返回 false。
     */
    BA_API bool permissionExists(const std::string& name);
    /**
     * @brief 获取所有已注册的权限。
     * @return 包含所有权限名称的字符串向量。
     */
    BA_API std::vector<std::string> getAllPermissions();

    // --- 组管理 ---
    /**
     * @brief 创建一个权限组。
     * @param groupName 组名称。
     * @param description 组描述（默认为空）。
     * @return 如果创建成功则返回 true，否则返回 false。
     */
    BA_API bool createGroup(const std::string& groupName, const std::string& description = "");
    /**
     * @brief 删除一个权限组。
     * @param groupName 组名称。
     * @return 如果删除成功则返回 true，否则返回 false。
     */
    BA_API bool deleteGroup(const std::string& groupName);
    /**
     * @brief 检查权限组是否存在。
     * @param groupName 组名称。
     * @return 如果组存在则返回 true，否则返回 false。
     */
    BA_API bool groupExists(const std::string& groupName);
    /**
     * @brief 获取所有权限组。
     * @return 包含所有组名称的字符串向量。
     */
    BA_API std::vector<std::string> getAllGroups();
    /**
     * @brief 获取权限组的详细信息。
     * @param groupName 组名称。
     * @return 组的详细信息结构体。
     */
    BA_API GroupDetails             getGroupDetails(const std::string& groupName);
    /**
     * @brief 更新权限组的描述。
     * @param groupName 组名称。
     * @param newDescription 新的组描述。
     * @return 如果更新成功则返回 true，否则返回 false。
     */
    BA_API bool updateGroupDescription(const std::string& groupName, const std::string& newDescription);
    /**
     * @brief 获取权限组的描述。
     * @param groupName 组名称。
     * @return 组的描述字符串。
     */
    BA_API std::string getGroupDescription(const std::string& groupName);

    // --- 组权限管理 ---
    /**
     * @brief 向权限组添加一个权限。
     * @param groupName 组名称。
     * @param permissionName 权限名称。
     * @return 如果添加成功则返回 true，否则返回 false。
     */
    BA_API bool addPermissionToGroup(const std::string& groupName, const std::string& permissionName);
    /**
     * @brief 从权限组中移除一个权限。
     * @param groupName 组名称。
     * @param permissionName 权限名称。
     * @return 如果移除成功则返回 true，否则返回 false。
     */
    BA_API bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionName);
    /**
     * @brief 获取权限组直接拥有的权限。
     * @param groupName 组名称。
     * @return 包含直接权限名称的字符串向量。
     */
    BA_API std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName);
    /**
     * @brief 获取权限组的所有编译后的权限规则（包括继承的）。
     * @param groupName 组名称。
     * @return 包含编译后权限规则的向量。
     */
    BA_API std::vector<CompiledPermissionRule> getPermissionsOfGroup(const std::string& groupName);
    /**
     * @brief 向权限组批量添加权限规则。
     * @param groupName 组名称。
     * @param permissionRules 权限规则字符串向量。
     * @return 成功添加的权限规则数量。
     */
    BA_API size_t addPermissionsToGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);
    /**
     * @brief 从权限组批量移除权限规则。
     * @param groupName 组名称。
     * @param permissionRules 权限规则字符串向量。
     * @return 成功移除的权限规则数量。
     */
    BA_API size_t
    removePermissionsFromGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);

    // --- 组继承管理 ---
    /**
     * @brief 添加组继承关系。
     * @param groupName 子组名称。
     * @param parentGroupName 父组名称。
     * @return 如果添加成功则返回 true，否则返回 false。
     */
    BA_API bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /**
     * @brief 移除组继承关系。
     * @param groupName 子组名称。
     * @param parentGroupName 父组名称。
     * @return 如果移除成功则返回 true，否则返回 false。
     */
    BA_API bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /**
     * @brief 获取权限组的所有祖先组（包括间接继承的）。
     * @param groupName 组名称。
     * @return 包含所有祖先组名称的字符串向量。
     */
    BA_API std::vector<std::string> getAllAncestorGroups(const std::string& groupName);
    /**
     * @brief 获取权限组的直接父组。
     * @param groupName 组名称。
     * @return 包含直接父组名称的字符串向量。
     */
    BA_API std::vector<std::string> getDirectParentGroups(const std::string& groupName);

    // --- 组优先级管理 ---
    /**
     * @brief 设置权限组的优先级。
     * @param groupName 组名称。
     * @param priority 优先级值。
     * @return 如果设置成功则返回 true，否则返回 false。
     */
    BA_API bool setGroupPriority(const std::string& groupName, int priority);
    /**
     * @brief 获取权限组的优先级。
     * @param groupName 组名称。
     * @return 组的优先级值。
     */
    BA_API int  getGroupPriority(const std::string& groupName);

    // --- 玩家管理 ---
    /**
     * @brief 将玩家添加到权限组。
     * @param playerUuid 玩家的 UUID。
     * @param groupName 组名称。
     * @return 如果添加成功则返回 true，否则返回 false。
     */
    BA_API bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName);
    BA_API bool addPlayerToGroup(
        const std::string& playerUuid,
        const std::string& groupName,
        long long          durationSeconds
    ); // 新增：带过期时间的方法，durationSeconds 为 0 或负数表示永不过期
    /**
     * @brief 将玩家从权限组中移除。
     * @param playerUuid 玩家的 UUID。
     * @param groupName 组名称。
     * @return 如果移除成功则返回 true，否则返回 false。
     */
    BA_API bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName);
    /**
     * @brief 获取玩家所属的所有权限组。
     * @param playerUuid 玩家的 UUID。
     * @return 包含玩家所属组名称的字符串向量。
     */
    BA_API std::vector<std::string> getPlayerGroups(const std::string& playerUuid);
    /**
     * @brief 获取玩家所属的所有权限组 ID。
     * @param playerUuid 玩家的 UUID。
     * @return 包含玩家所属组 ID 的字符串向量。
     */
    BA_API std::vector<std::string> getPlayerGroupIds(const std::string& playerUuid);
    /**
     * @brief 获取指定权限组中的所有玩家 UUID。
     * @param groupName 组名称。
     * @return 包含组中所有玩家 UUID 的字符串向量。
     */
    BA_API std::vector<std::string> getPlayersInGroup(const std::string& groupName);
    /**
     * @brief 获取玩家所属的所有权限组及其优先级。
     * @param playerUuid 玩家的 UUID。
     * @return 包含玩家所属组详细信息（包括优先级）的向量。
     */
    BA_API std::vector<GroupDetails> getPlayerGroupsWithPriorities(const std::string& playerUuid);
    /**
     * @brief 将玩家批量添加到权限组。
     * @param playerUuid 玩家的 UUID。
     * @param groupNames 组名称字符串向量。
     * @return 成功添加的组数量。
     */
    BA_API size_t addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);
    /**
     * @brief 将玩家从权限组中批量移除。
     * @param playerUuid 玩家的 UUID。
     * @param groupNames 组名称字符串向量。
     * @return 成功移除的组数量。
     */
    BA_API size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);

    // --- 权限检查 ---
    /**
     * @brief 获取玩家的所有编译后的权限规则（包括继承的）。
     * @param playerUuid 玩家的 UUID。
     * @return 包含编译后权限规则的向量。
     */
    BA_API std::vector<CompiledPermissionRule> getAllPermissionsForPlayer(const std::string& playerUuid);
    /**
     * @brief 检查玩家是否拥有某个权限。
     * @param playerUuid 玩家的 UUID。
     * @param permissionNode 权限节点。
     * @return 如果玩家拥有该权限则返回 true，否则返回 false。
     */
    BA_API bool hasPermission(const std::string& playerUuid, const std::string& permissionNode);
    BA_API void runPeriodicCleanup(); // 新增：一个公共方法来触发清理任务

private:
    // PIMPL: 前向声明实现类
    class PermissionManagerImpl;
    // 使用 unique_ptr 管理实现类的生命周期
    std::unique_ptr<PermissionManagerImpl> m_pimpl;

    // 私有构造函数/析构函数，在 .cpp 文件中定义
    // 对于使用不完整类型 (incomplete type) 的 std::unique_ptr，这是必需的。
    PermissionManager();
    ~PermissionManager();

    // 禁用拷贝/移动构造函数和赋值运算符，以强制执行单例模式
    PermissionManager(const PermissionManager&)            = delete;
    PermissionManager& operator=(const PermissionManager&) = delete;
    PermissionManager(PermissionManager&&)                 = delete;
    PermissionManager& operator=(PermissionManager&&)      = delete;
};

} // namespace permission
} // namespace BA
