#pragma once

#include "permission/PermissionData.h"
#include <cstdint>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace BA {
namespace permission {
namespace internal {

class PermissionCache {
public:
    static constexpr size_t kMaxPlayerCacheEntries    = 4096;
    static constexpr size_t kMaxGroupPermCacheEntries = 2048;

    // 组名 <-> 组ID 映射缓存
    uint64_t                                     groupMappingEpoch() const;
    std::optional<std::string>                   findGroupId(const std::string& groupName) const;
    std::optional<std::string>                   findGroupName(const std::string& groupId) const;
    void                                         storeGroup(const std::string& groupName, const std::string& groupId);
    void                                         storeGroupIfEpoch(
        const std::string& groupName,
        const std::string& groupId,
        uint64_t           expectedEpoch
    );
    void                                         invalidateGroup(const std::string& groupName);
    void                                         populateAllGroups(std::unordered_map<std::string, std::string>&& groupNameMap);
    std::unordered_map<std::string, std::string> getAllGroups() const;

    // 玩家权限派生缓存（带版本号防陈旧回填 + 容量上限）
    uint64_t                                           playerPermissionsEpoch() const;
    std::optional<std::vector<CompiledPermissionRule>> findPlayerPermissions(const std::string& playerUuid) const;
    void                                               storePlayerPermissions(
        const std::string&                         playerUuid,
        const std::vector<CompiledPermissionRule>& permissions,
        std::optional<long long>                   earliestExpiry,
        std::vector<std::string>                   sourceGroups,
        uint64_t                                   expectedEpoch
    );
    void invalidatePlayerPermissions(const std::string& playerUuid);
    void invalidatePlayerPermissionsForGroups(const std::set<std::string>& affectedGroups);
    void invalidateAllPlayerPermissions();

    // 玩家组派生缓存（带版本号防陈旧回填 + 容量上限）
    uint64_t                                   playerGroupsEpoch() const;
    std::optional<std::vector<GroupDetails>>   findPlayerGroups(const std::string& playerUuid) const;
    void                                       storePlayerGroups(
        const std::string&               playerUuid,
        const std::vector<GroupDetails>& groups,
        uint64_t                         expectedEpoch
    );
    void invalidatePlayerGroups(const std::string& playerUuid);
    void invalidatePlayerGroupsForGroup(const std::string& groupName);
    void invalidateAllPlayerGroups();

    // 组权限派生缓存（带版本号防陈旧回填 + 容量上限）
    uint64_t                                           groupPermissionsEpoch() const;
    std::optional<std::vector<CompiledPermissionRule>> findGroupPermissions(const std::string& groupName) const;
    void                                               storeGroupPermissions(
        const std::string&                         groupName,
        const std::vector<CompiledPermissionRule>& permissions,
        uint64_t                                   expectedEpoch
    );
    void invalidateGroupPermissions(const std::string& groupName);
    void invalidateGroupPermissionsForGroups(const std::set<std::string>& groupNames);
    void invalidateAllGroupPermissions();

    // 默认权限权威内存索引（init 时全量加载）
    bool                                  arePermissionDefaultsLoaded() const;
    std::optional<bool>                   findPermissionDefault(const std::string& permissionName) const;
    void                                  storePermissionDefault(const std::string& permissionName, bool defaultValue);
    void                                  populateAllPermissionDefaults(std::unordered_map<std::string, bool>&& defaultsMap);
    std::unordered_map<std::string, bool> getAllPermissionDefaults() const;

    // 继承图权威内存索引（init 时全量加载）
    void populateInheritance(
        std::unordered_map<std::string, std::set<std::string>>&& parentToChildren,
        std::unordered_map<std::string, std::set<std::string>>&& childToParents
    );
    void                     addInheritance(const std::string& child, const std::string& parent);
    void                     removeInheritance(const std::string& child, const std::string& parent);
    void                     removeGroupFromInheritance(const std::string& groupName);
    bool                     canAddInheritance(const std::string& child, const std::string& parent) const;
    std::vector<std::string> getDirectParentGroups(const std::string& groupName) const;
    std::set<std::string>    getAllAncestorGroups(const std::string& groupName) const;
    std::set<std::string>    getChildGroupsRecursive(const std::string& groupName) const;

private:
    struct PlayerPermissionEntry {
        std::vector<CompiledPermissionRule> rules;
        std::optional<long long>            earliestExpiry;
        std::vector<std::string>            sourceGroups;
        uint64_t                            insertedSeq = 0;
    };

    struct PlayerGroupEntry {
        std::vector<GroupDetails> groups;
        uint64_t                  insertedSeq = 0;
    };

    struct GroupPermissionEntry {
        std::vector<CompiledPermissionRule> rules;
        uint64_t                            insertedSeq = 0;
    };

    // 组映射状态
    std::unordered_map<std::string, std::string> m_groupNameCache;
    std::unordered_map<std::string, std::string> m_groupIdCache;
    uint64_t                                     m_groupMappingEpoch = 0;
    mutable std::shared_mutex                    m_groupMappingMutex;

    // 玩家权限派生缓存
    std::unordered_map<std::string, PlayerPermissionEntry> m_playerPermissionsCache;
    uint64_t                                               m_playerPermsEpoch = 0;
    uint64_t                                               m_playerPermsSeq   = 0;
    mutable std::shared_mutex                              m_playerPermissionsMutex;

    // 玩家组派生缓存
    std::unordered_map<std::string, PlayerGroupEntry> m_playerGroupsCache;
    uint64_t                                          m_playerGroupsEpoch = 0;
    uint64_t                                          m_playerGroupsSeq   = 0;
    mutable std::shared_mutex                         m_playerGroupsMutex;

    // 组权限派生缓存
    std::unordered_map<std::string, GroupPermissionEntry> m_groupPermissionsCache;
    uint64_t                                              m_groupPermsEpoch = 0;
    uint64_t                                              m_groupPermsSeq   = 0;
    mutable std::shared_mutex                             m_groupPermissionsMutex;

    // 默认权限权威索引
    std::unordered_map<std::string, bool> m_permissionDefaultsCache;
    bool                                  m_permissionDefaultsLoaded = false;
    mutable std::shared_mutex             m_permissionDefaultsMutex;

    // 继承图权威索引
    std::unordered_map<std::string, std::set<std::string>> m_parentToChildren;
    std::unordered_map<std::string, std::set<std::string>> m_childToParents;
    mutable std::shared_mutex                              m_inheritanceMutex;
};

} // namespace internal
} // namespace permission
} // namespace BA
