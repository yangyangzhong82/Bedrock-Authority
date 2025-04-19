#pragma once

#include <string>
#include <vector>
#include "db/IDatabase.h"

namespace BA {
namespace permission {

class PermissionManager {
public:
    static PermissionManager& getInstance();

    /// Initialize the manager with a database instance (must call before other APIs).
    void init(db::IDatabase* db);

    /// Register a new permission. Returns false if already exists or on error.
    bool registerPermission(const std::string& name, const std::string& description = "", bool defaultValue = false);
    /// Check if permission exists
    bool permissionExists(const std::string& name);
    /// Get all permission names
    std::vector<std::string> getAllPermissions();

    /// Create a permission group. Returns false if already exists or on error.
    bool createGroup(const std::string& groupName, const std::string& description = "");
    /// Check if group exists
    bool groupExists(const std::string& groupName);
    /// Get all group names
    std::vector<std::string> getAllGroups();

    /// Add permission to group. Returns false if group or permission not exist, or already assigned.
    bool addPermissionToGroup(const std::string& groupName, const std::string& permissionName);
    /// Remove permission from group
    bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionName);
    /// Get permissions of a group (includes inherited)
    std::vector<std::string> getPermissionsOfGroup(const std::string& groupName);

    /// Add inheritance: child inherits parent. Returns false if invalid or already set.
    bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /// Remove inheritance
    bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /// Get direct parent groups
    std::vector<std::string> getParentGroups(const std::string& groupName);

    /// Assign a player to a permission group
    bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName);
    /// Remove a player from a permission group
    bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName);
    /// Get groups a player belongs to
    std::vector<std::string> getPlayerGroups(const std::string& playerUuid);
    /// Get players in a permission group
    std::vector<std::string> getPlayersInGroup(const std::string& groupName);

    /// Set priority for a permission group (higher priority wins)
    bool setGroupPriority(const std::string& groupName, int priority);
    /// Get priority of a permission group
    int getGroupPriority(const std::string& groupName);
    /// Check if a player has a specific permission (supports wildcard and negation)
    bool hasPermission(const std::string& playerUuid, const std::string& permissionNode);

private:
    PermissionManager() = default;
    void ensureTables();
    // Helper to get ID from name for permissions or groups table
    std::string getIdByName(const std::string& table, const std::string& name);

    db::IDatabase* db_ = nullptr;
};

} // namespace permission
} // namespace BA
