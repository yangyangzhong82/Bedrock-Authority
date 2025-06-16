#pragma once

#include "permission/PermissionData.h"
#include <optional>
#include <shared_mutex>
#include <unordered_map>


namespace BA {
namespace permission {
namespace internal {

class PermissionCache {
public:
    // Group Name/ID Cache
    std::optional<std::string> findGroupId(const std::string& groupName);
    void                       storeGroup(const std::string& groupName, const std::string& groupId);
    void                       invalidateGroup(const std::string& groupName);
    void                       populateAllGroups(std::unordered_map<std::string, std::string>&& groupNameMap);
    const std::unordered_map<std::string, std::string>& getAllGroups() const;

    // Player Permissions Cache
    std::optional<std::vector<CompiledPermissionRule>> findPlayerPermissions(const std::string& playerUuid);
    void storePlayerPermissions(const std::string& playerUuid, const std::vector<CompiledPermissionRule>& permissions);
    void invalidatePlayerPermissions(const std::string& playerUuid);
    void invalidateAllPlayerPermissions();

    // Player Groups Cache
    std::optional<std::vector<GroupDetails>> findPlayerGroups(const std::string& playerUuid);
    void storePlayerGroups(const std::string& playerUuid, const std::vector<GroupDetails>& groups);
    void invalidatePlayerGroups(const std::string& playerUuid);

    // Group Permissions Cache
    std::optional<std::vector<CompiledPermissionRule>> findGroupPermissions(const std::string& groupName);
    void storeGroupPermissions(const std::string& groupName, const std::vector<CompiledPermissionRule>& permissions);
    void invalidateGroupPermissions(const std::string& groupName);
    void invalidateAllGroupPermissions();

    // Permission Defaults Cache
    std::optional<bool> findPermissionDefault(const std::string& permissionName);
    void                storePermissionDefault(const std::string& permissionName, bool defaultValue);
    void                populateAllPermissionDefaults(std::unordered_map<std::string, bool>&& defaultsMap);
    const std::unordered_map<std::string, bool>& getAllPermissionDefaults() const;

    // Inheritance Cache
    void populateInheritance(
        std::unordered_map<std::string, std::set<std::string>>&& parentToChildren,
        std::unordered_map<std::string, std::set<std::string>>&& childToParents
    );
    void                  addInheritance(const std::string& child, const std::string& parent);
    void                  removeInheritance(const std::string& child, const std::string& parent);
    bool                  hasPath(const std::string& startNode, const std::string& endNode) const;
    std::set<std::string> getAllAncestorGroups(const std::string& groupName) const;
    std::set<std::string> getChildGroupsRecursive(const std::string& groupName) const;

private:
    // Caches
    std::unordered_map<std::string, std::string>                         m_groupNameCache;
    std::unordered_map<std::string, std::vector<CompiledPermissionRule>> m_playerPermissionsCache;
    std::unordered_map<std::string, std::vector<GroupDetails>>           m_playerGroupsCache;
    std::unordered_map<std::string, std::vector<CompiledPermissionRule>> m_groupPermissionsCache;
    std::unordered_map<std::string, bool>                                m_permissionDefaultsCache;
    std::unordered_map<std::string, std::set<std::string>>               m_parentToChildren;
    std::unordered_map<std::string, std::set<std::string>>               m_childToParents;

    // Mutexes
    mutable std::shared_mutex m_groupNameMutex;
    mutable std::shared_mutex m_playerPermissionsMutex;
    mutable std::shared_mutex m_playerGroupsMutex;
    mutable std::shared_mutex m_groupPermissionsMutex;
    mutable std::shared_mutex m_permissionDefaultsMutex;
    mutable std::shared_mutex m_inheritanceMutex;
};

} // namespace internal
} // namespace permission
} // namespace BA
