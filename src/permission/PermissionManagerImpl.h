#pragma once

#include "permission/PermissionData.h"    // 包含权限数据结构定义
#include "permission/PermissionManager.h" // 包含主类的定义以访问其作用域
#include <memory>                         // 包含智能指针
#include <regex>                          // 包含正则表达式库
#include <string>                         // 包含字符串类型
#include <vector>                         // 包含向量类型


// 前向声明内部类，以避免在头文件中包含它们的完整定义，减少编译依赖
namespace BA {
namespace db {
class IDatabase; // 数据库接口前向声明
}
namespace permission {
namespace internal {
class PermissionStorage;     // 权限存储类前向声明
class PermissionCache;       // 权限缓存类前向声明
class AsyncCacheInvalidator; // 异步缓存失效器前向声明
} // namespace internal
} // namespace permission
} // namespace BA


namespace BA {
namespace permission {

// 这是 PermissionManager 的 PIMPL (Pointer to Implementation) 实现类。
// 它位于 PermissionManager 类的作用域内，但定义在一个单独的头文件中，
// 旨在隐藏实现细节，减少编译依赖，并提高模块化。
class PermissionManager::PermissionManagerImpl {
public:
    // 构造函数
    PermissionManagerImpl();
    // 析构函数
    ~PermissionManagerImpl();

    // --- 核心生命周期管理 ---
    /**
     * @brief 初始化权限管理器实现。
     * @param db 数据库接口指针。
     * @param enableWarmup 是否启用预热缓存。
     * @param threadPoolSize 线程池大小。
     * @return 如果初始化成功则返回 true，否则返回 false。
     */
    bool init(db::IDatabase* db, bool enableWarmup, unsigned int threadPoolSize);
    /**
     * @brief 关闭权限管理器实现。
     */
    void shutdown();

    // --- 权限管理功能 ---
    /**
     * @brief 注册一个权限。
     * @param name 权限名称。
     * @param description 权限描述。
     * @param defaultValue 权限的默认值。
     * @return 如果注册成功则返回 true，否则返回 false。
     */
    bool registerPermission(const std::string& name, const std::string& description, bool defaultValue);
    /**
     * @brief 检查权限是否存在。
     * @param name 权限名称。
     * @return 如果权限存在则返回 true，否则返回 false。
     */
    bool permissionExists(const std::string& name);
    /**
     * @brief 获取所有已注册的权限名称。
     * @return 包含所有权限名称的向量。
     */
    std::vector<std::string> getAllPermissions();

    // --- 组管理功能 ---
    /**
     * @brief 创建一个新组。
     * @param groupName 组名称。
     * @param description 组描述。
     * @return 如果创建成功则返回 true，否则返回 false。
     */
    bool createGroup(const std::string& groupName, const std::string& description);
    /**
     * @brief 删除一个组。
     * @param groupName 要删除的组名称。
     * @return 如果删除成功则返回 true，否则返回 false。
     */
    bool deleteGroup(const std::string& groupName);
    /**
     * @brief 检查组是否存在。
     * @param groupName 组名称。
     * @return 如果组存在则返回 true，否则返回 false。
     */
    bool groupExists(const std::string& groupName);
    /**
     * @brief 获取所有组的名称。
     * @return 包含所有组名称的向量。
     */
    std::vector<std::string> getAllGroups();
    /**
     * @brief 获取组的详细信息。
     * @param groupName 组名称。
     * @return 组的详细信息对象。
     */
    GroupDetails getGroupDetails(const std::string& groupName);
    /**
     * @brief 更新组的描述。
     * @param groupName 组名称。
     * @param newDescription 新的组描述。
     * @return 如果更新成功则返回 true，否则返回 false。
     */
    bool updateGroupDescription(const std::string& groupName, const std::string& newDescription);
    /**
     * @brief 获取组的描述。
     * @param groupName 组名称。
     * @return 组的描述字符串。
     */
    std::string getGroupDescription(const std::string& groupName);

    // --- 组权限管理 ---
    /**
     * @brief 向组添加权限规则。
     * @param groupName 组名称。
     * @param permissionRule 权限规则字符串。
     * @return 如果添加成功则返回 true，否则返回 false。
     */
    bool addPermissionToGroup(const std::string& groupName, const std::string& permissionRule);
    /**
     * @brief 从组中移除权限规则。
     * @param groupName 组名称。
     * @param permissionRule 权限规则字符串。
     * @return 如果移除成功则返回 true，否则返回 false。
     */
    bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionRule);
    /**
     * @brief 获取组的直接权限。
     * @param groupName 组名称。
     * @return 包含组直接权限规则的向量。
     */
    std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName);
    /**
     * @brief 获取组的所有有效权限（包括继承的权限）。
     * @param groupName 组名称。
     * @return 包含编译后的权限规则的向量。
     */
    std::vector<CompiledPermissionRule> getPermissionsOfGroup(const std::string& groupName);
    /**
     * @brief 批量向组添加权限规则。
     * @param groupName 组名称。
     * @param permissionRules 权限规则字符串向量。
     * @return 成功添加的权限规则数量。
     */
    size_t addPermissionsToGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);
    /**
     * @brief 批量从组中移除权限规则。
     * @param groupName 组名称。
     * @param permissionRules 权限规则字符串向量。
     * @return 成功移除的权限规则数量。
     */
    size_t removePermissionsFromGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);

    // --- 组继承管理 ---
    /**
     * @brief 添加组继承关系。
     * @param groupName 子组名称。
     * @param parentGroupName 父组名称。
     * @return 如果添加成功则返回 true，否则返回 false。
     */
    bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /**
     * @brief 移除组继承关系。
     * @param groupName 子组名称。
     * @param parentGroupName 父组名称。
     * @return 如果移除成功则返回 true，否则返回 false。
     */
    bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /**
     * @brief 获取组的所有祖先组（包括自身）。
     * @param groupName 组名称。
     * @return 包含所有祖先组名称的向量。
     */
    std::vector<std::string> getAllAncestorGroups(const std::string& groupName);
    /**
     * @brief 获取组的直接父组。
     * @param groupName 组名称。
     * @return 包含直接父组名称的向量。
     */
    std::vector<std::string> getDirectParentGroups(const std::string& groupName);

    // --- 组优先级管理 ---
    /**
     * @brief 设置组的优先级。
     * @param groupName 组名称。
     * @param priority 新的优先级值。
     * @return 如果设置成功则返回 true，否则返回 false。
     */
    bool setGroupPriority(const std::string& groupName, int priority);
    /**
     * @brief 获取组的优先级。
     * @param groupName 组名称。
     * @return 组的优先级值。
     */
    int getGroupPriority(const std::string& groupName);

    // --- 玩家管理功能 ---
    /**
     * @brief 将玩家添加到组。
     * @param playerUuid 玩家的 UUID。
     * @param groupName 组名称。
     * @return 如果添加成功则返回 true，否则返回 false。
     */
    bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName);
    bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName, long long durationSeconds);
    /**
     * @brief 将玩家从组中移除。
     * @param playerUuid 玩家的 UUID。
     * @param groupName 组名称。
     * @return 如果移除成功则返回 true，否则返回 false。
     */
    bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName);
    /**
     * @brief 获取玩家所属的所有组名称。
     * @param playerUuid 玩家的 UUID。
     * @return 包含玩家所属组名称的向量。
     */
    std::vector<std::string> getPlayerGroups(const std::string& playerUuid);
    /**
     * @brief 获取玩家所属的所有组 ID。
     * @param playerUuid 玩家的 UUID。
     * @return 包含玩家所属组 ID 的向量。
     */
    std::vector<std::string> getPlayerGroupIds(const std::string& playerUuid);
    /**
     * @brief 获取组中的所有玩家 UUID。
     * @param groupName 组名称。
     * @return 包含组中所有玩家 UUID 的向量。
     */
    std::vector<std::string> getPlayersInGroup(const std::string& groupName);
    /**
     * @brief 获取玩家所属的组及其优先级。
     * @param playerUuid 玩家的 UUID。
     * @return 包含玩家所属组详细信息（包括优先级）的向量。
     */
    std::vector<GroupDetails> getPlayerGroupsWithPriorities(const std::string& playerUuid);
    /**
     * @brief 批量将玩家添加到多个组。
     * @param playerUuid 玩家的 UUID。
     * @param groupNames 组名称向量。
     * @return 成功添加的组数量。
     */
    size_t addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);
    /**
     * @brief 批量将玩家从多个组中移除。
     * @param playerUuid 玩家的 UUID。
     * @param groupNames 组名称向量。
     * @return 成功移除的组数量。
     */
    size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);

    // --- 权限检查功能 ---
    /**
     * @brief 获取玩家的所有有效权限（包括组权限和默认权限）。
     * @param playerUuid 玩家的 UUID。
     * @return 包含编译后的权限规则的向量。
     */
    std::vector<CompiledPermissionRule> getAllPermissionsForPlayer(const std::string& playerUuid);
    /**
     * @brief 检查玩家是否拥有某个权限。
     * @param playerUuid 玩家的 UUID。
     * @param permissionNode 要检查的权限节点。
     * @return 如果玩家拥有该权限则返回 true，否则返回 false。
     */
    bool hasPermission(const std::string& playerUuid, const std::string& permissionNode);
    void runPeriodicCleanup();

private:
    // --- 辅助方法 ---
    /**
     * @brief 获取缓存的组 ID，如果缓存中不存在则从存储层获取并缓存。
     * @param groupName 组名称。
     * @return 组的 ID 字符串。
     */
    std::string getCachedGroupId(const std::string& groupName);
    /**
     * @brief 将通配符模式转换为正则表达式。
     * @param pattern 包含通配符的字符串模式。
     * @return 对应的正则表达式对象。
     */
    std::regex wildcardToRegex(const std::string& pattern);
    /**
     * @brief 预热所有权限缓存。
     *        此方法会从存储层加载数据并填充到缓存中，以提高后续查询性能。
     */
    void populateAllCaches();

    // --- 成员变量 ---
    // 使用 unique_ptr 来管理内部组件的生命周期，确保资源自动释放
    std::unique_ptr<internal::PermissionStorage>     m_storage;     // 权限存储组件
    std::unique_ptr<internal::PermissionCache>       m_cache;       // 权限缓存组件
    std::unique_ptr<internal::AsyncCacheInvalidator> m_invalidator; // 异步缓存失效器组件

    bool m_initialized = false; // 标记权限管理器是否已初始化
};

} // namespace permission
} // namespace BA
