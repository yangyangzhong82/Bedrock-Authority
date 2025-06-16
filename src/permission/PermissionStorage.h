#pragma once

#include "permission/PermissionData.h"
#include <string>
#include <unordered_map>
#include <vector>


namespace BA {
namespace db {
class IDatabase;
} // namespace db

namespace permission {
namespace internal {

class PermissionStorage {
public:
    explicit PermissionStorage(db::IDatabase* db);

    bool ensureTables();

    // Permissions
    bool upsertPermission(const std::string& name, const std::string& description, bool defaultValue);
    bool permissionExists(const std::string& name);
    std::vector<std::string>              fetchAllPermissionNames();
    std::vector<std::string>              fetchDefaultPermissionNames();
    std::unordered_map<std::string, bool> fetchAllPermissionDefaults();

    // Groups
    bool        createGroup(const std::string& groupName, const std::string& description, std::string& outGroupId);
    bool        deleteGroup(const std::string& groupId);
    std::string fetchGroupIdByName(const std::string& groupName);
    std::vector<std::string> fetchAllGroupNames();
    bool                     groupExists(const std::string& groupName);
    GroupDetails             fetchGroupDetails(const std::string& groupName);
    int                      fetchGroupPriority(const std::string& groupName);
    bool                     updateGroupPriority(const std::string& groupName, int priority);
    bool                     updateGroupDescription(const std::string& groupName, const std::string& newDescription);
    std::string              fetchGroupDescription(const std::string& groupName);

    // Group Permissions
    bool                     addPermissionToGroup(const std::string& groupId, const std::string& permissionRule);
    bool                     removePermissionFromGroup(const std::string& groupId, const std::string& permissionRule);
    std::vector<std::string> fetchDirectPermissionsOfGroup(const std::string& groupId);
    size_t addPermissionsToGroup(const std::string& groupId, const std::vector<std::string>& permissionRules);
    size_t removePermissionsFromGroup(const std::string& groupId, const std::vector<std::string>& permissionRules);

    // Inheritance
    bool addGroupInheritance(const std::string& groupId, const std::string& parentGroupId);
    bool removeGroupInheritance(const std::string& groupId, const std::string& parentGroupId);
    std::unordered_map<std::string, std::set<std::string>> fetchAllInheritance();
    std::vector<std::string> fetchDirectParentGroupIds(const std::string& groupId);

    // Player Groups
    bool                      addPlayerToGroup(const std::string& playerUuid, const std::string& groupId);
    bool                      removePlayerFromGroup(const std::string& playerUuid, const std::string& groupId);
    std::vector<GroupDetails> fetchPlayerGroupsWithDetails(const std::string& playerUuid);
    std::vector<std::string>  fetchPlayersInGroup(const std::string& groupId);
    std::vector<std::string>  fetchPlayersInGroups(const std::vector<std::string>& groupIds); // New: Fetch players in multiple groups
    std::unordered_map<std::string, std::string> fetchGroupIdsByNames(const std::set<std::string>& groupNames); // New: Fetch group IDs by names
    std::unordered_map<std::string, std::string> fetchGroupNamesByIds(const std::vector<std::string>& groupIds); // New: Fetch group names by IDs

    size_t                    addPlayerToGroups(
                           const std::string&                                      playerUuid,
                           const std::vector<std::pair<std::string, std::string>>& groupInfos
                       ); // pair<groupName, groupId>
    size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupIds);

private:
    db::IDatabase* m_db = nullptr;
};

} // namespace internal
} // namespace permission
} // namespace BA
