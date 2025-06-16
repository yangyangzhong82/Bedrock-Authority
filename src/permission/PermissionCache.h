#pragma once

#include "permission/PermissionData.h" // 包含权限数据结构定义
#include <optional>                     // 用于可选值
#include <shared_mutex>                 // 用于读写锁
#include <unordered_map>                // 用于哈希表


namespace BA {
namespace permission {
namespace internal {

// 权限缓存类，用于存储和管理各种权限相关的数据
class PermissionCache {
public:
    // 组名/ID 缓存
    // 根据组名查找组ID
    std::optional<std::string> findGroupId(const std::string& groupName);
    // 根据组ID查找组名
    std::optional<std::string> findGroupName(const std::string& groupId);
    // 存储组名和组ID的映射关系
    void                       storeGroup(const std::string& groupName, const std::string& groupId);
    // 使指定组的缓存失效
    void                       invalidateGroup(const std::string& groupName);
    // 批量填充所有组的缓存
    void                       populateAllGroups(std::unordered_map<std::string, std::string>&& groupNameMap);
    // 获取所有组的映射关系
    const std::unordered_map<std::string, std::string>& getAllGroups() const;

    // 玩家权限缓存
    // 查找指定玩家的权限
    std::optional<std::vector<CompiledPermissionRule>> findPlayerPermissions(const std::string& playerUuid);
    // 存储指定玩家的权限
    void storePlayerPermissions(const std::string& playerUuid, const std::vector<CompiledPermissionRule>& permissions);
    // 使指定玩家的权限缓存失效
    void invalidatePlayerPermissions(const std::string& playerUuid);
    // 使所有玩家的权限缓存失效
    void invalidateAllPlayerPermissions();

    // 玩家组缓存
    // 查找指定玩家所属的组
    std::optional<std::vector<GroupDetails>> findPlayerGroups(const std::string& playerUuid);
    // 存储指定玩家所属的组
    void storePlayerGroups(const std::string& playerUuid, const std::vector<GroupDetails>& groups);
    // 使指定玩家的组缓存失效
    void invalidatePlayerGroups(const std::string& playerUuid);

    // 组权限缓存
    // 查找指定组的权限
    std::optional<std::vector<CompiledPermissionRule>> findGroupPermissions(const std::string& groupName);
    // 存储指定组的权限
    void storeGroupPermissions(const std::string& groupName, const std::vector<CompiledPermissionRule>& permissions);
    // 使指定组的权限缓存失效
    void invalidateGroupPermissions(const std::string& groupName);
    // 使所有组的权限缓存失效
    void invalidateAllGroupPermissions();

    // 默认权限缓存
    // 查找指定权限的默认值
    std::optional<bool> findPermissionDefault(const std::string& permissionName);
    // 存储指定权限的默认值
    void                storePermissionDefault(const std::string& permissionName, bool defaultValue);
    // 批量填充所有默认权限的缓存
    void                populateAllPermissionDefaults(std::unordered_map<std::string, bool>&& defaultsMap);
    // 获取所有默认权限的映射关系
    const std::unordered_map<std::string, bool>& getAllPermissionDefaults() const;

    // 继承缓存
    // 填充继承关系缓存
    void populateInheritance(
        std::unordered_map<std::string, std::set<std::string>>&& parentToChildren, // 父组到子组的映射
        std::unordered_map<std::string, std::set<std::string>>&& childToParents    // 子组到父组的映射
    );
    // 添加继承关系
    void                  addInheritance(const std::string& child, const std::string& parent);
    // 移除继承关系
    void                  removeInheritance(const std::string& child, const std::string& parent);
    // 检查两个组之间是否存在继承路径
    bool                  hasPath(const std::string& startNode, const std::string& endNode) const;
    // 获取指定组的所有祖先组（包括自身）
    std::set<std::string> getAllAncestorGroups(const std::string& groupName) const;
    // 递归获取指定组的所有子组（包括自身）
    std::set<std::string> getChildGroupsRecursive(const std::string& groupName) const;

private:
    // 缓存数据结构
    std::unordered_map<std::string, std::string>                         m_groupNameCache;        // 组名到组ID的映射
    std::unordered_map<std::string, std::string>                         m_groupIdCache;          // 组ID到组名的映射
    std::unordered_map<std::string, std::vector<CompiledPermissionRule>> m_playerPermissionsCache; // 玩家UUID到编译权限规则的映射
    std::unordered_map<std::string, std::vector<GroupDetails>>           m_playerGroupsCache;      // 玩家UUID到组详情的映射
    std::unordered_map<std::string, std::vector<CompiledPermissionRule>> m_groupPermissionsCache;  // 组名到编译权限规则的映射
    std::unordered_map<std::string, bool>                                m_permissionDefaultsCache; // 权限名到默认值的映射
    std::unordered_map<std::string, std::set<std::string>>               m_parentToChildren;       // 父组到子组的映射
    std::unordered_map<std::string, std::set<std::string>>               m_childToParents;         // 子组到父组的映射

    // 互斥锁，用于保护缓存数据
    mutable std::shared_mutex m_groupNameMutex;        // 组名缓存的互斥锁
    mutable std::shared_mutex m_groupIdMutex;          // 组ID缓存的互斥锁
    mutable std::shared_mutex m_playerPermissionsMutex; // 玩家权限缓存的互斥锁
    mutable std::shared_mutex m_playerGroupsMutex;      // 玩家组缓存的互斥锁
    mutable std::shared_mutex m_groupPermissionsMutex;  // 组权限缓存的互斥锁
    mutable std::shared_mutex m_permissionDefaultsMutex; // 默认权限缓存的互斥锁
    mutable std::shared_mutex m_inheritanceMutex;       // 继承缓存的互斥锁
};

} // namespace internal
} // namespace permission
} // namespace BA
