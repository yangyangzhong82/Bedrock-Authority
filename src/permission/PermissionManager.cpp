#include "permission/PermissionManager.h"
#include <set>
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"
#include <algorithm>
#include <regex>

namespace BA { // Changed from my_mod
namespace permission {

PermissionManager& PermissionManager::getInstance() {
    static PermissionManager instance;
    return instance;
}

void PermissionManager::init(db::IDatabase* db) {
    db_ = db;
    ensureTables();
    ll::mod::NativeMod::current()->getLogger().info("PermissionManager initialized");
}

void PermissionManager::ensureTables() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Ensuring permission tables exist");
    // Add default_value column to permissions table if not exists
    db_->execute("CREATE TABLE IF NOT EXISTS permissions (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL, description TEXT, default_value INTEGER NOT NULL DEFAULT 0);");
    // Try adding the column in case the table already exists without it
    db_->execute("ALTER TABLE permissions ADD COLUMN default_value INTEGER NOT NULL DEFAULT 0;");
    db_->execute("CREATE TABLE IF NOT EXISTS permission_groups (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL, description TEXT);");
    db_->execute("CREATE TABLE IF NOT EXISTS group_permissions (group_id INTEGER NOT NULL, permission_id INTEGER NOT NULL, PRIMARY KEY (group_id, permission_id));");
    db_->execute("CREATE TABLE IF NOT EXISTS group_inheritance (group_id INTEGER NOT NULL, parent_group_id INTEGER NOT NULL, PRIMARY KEY (group_id, parent_group_id));");
    db_->execute("CREATE TABLE IF NOT EXISTS player_groups (player_uuid TEXT NOT NULL, group_id INTEGER NOT NULL, PRIMARY KEY (player_uuid, group_id));");
    // Add priority column to group table if not exists
    db_->execute("ALTER TABLE permission_groups ADD COLUMN priority INTEGER NOT NULL DEFAULT 0;");
}

// Helper function to get single ID, returns empty string if not found or error
std::string PermissionManager::getIdByName(const std::string& table, const std::string& name) {
    std::string sql = "SELECT id FROM " + table + " WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {name});
    if (rows.empty() || rows[0].empty()) {
        return "";
    }
    return rows[0][0];
}


bool PermissionManager::registerPermission(const std::string& name, const std::string& description, bool defaultValue) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Registering permission '%s' with default value %s", name.c_str(), defaultValue ? "true" : "false");
    std::string defaultValueStr = defaultValue ? "1" : "0";

    // Try inserting first
    std::string insertSql = "INSERT OR IGNORE INTO permissions (name, description, default_value) VALUES (?, ?, ?);";
    bool insertOk = db_->executePrepared(insertSql, {name, description, defaultValueStr});

    // Then update (in case it already existed)
    std::string updateSql = "UPDATE permissions SET description = ?, default_value = ? WHERE name = ?;";
    bool updateOk = db_->executePrepared(updateSql, {description, defaultValueStr, name});

    // Consider success if either operation logically succeeded (insert new or update existing)
    // A more robust check might involve checking affected rows if the DB API supports it.
    // For now, let's assume success if the execute calls didn't return false (indicating a DB error).
    return insertOk && updateOk; // Or potentially just updateOk if insert is IGNORE
}

bool PermissionManager::permissionExists(const std::string& name) {
    std::string sql = "SELECT 1 FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {name});
    return !rows.empty();
}

std::vector<std::string> PermissionManager::getAllPermissions() {
    std::vector<std::string> list;
    std::string sql = "SELECT name FROM permissions;";
    auto rows = db_->queryPrepared(sql, {}); // Empty params
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}

bool PermissionManager::createGroup(const std::string& groupName, const std::string& description) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Creating group '%s'", groupName.c_str());
    std::string sql = "INSERT OR IGNORE INTO permission_groups (name, description) VALUES (?, ?);";
    return db_->executePrepared(sql, {groupName, description});
}

bool PermissionManager::groupExists(const std::string& groupName) {
    std::string sql = "SELECT 1 FROM permission_groups WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {groupName});
    return !rows.empty();
}

std::vector<std::string> PermissionManager::getAllGroups() {
    std::vector<std::string> list;
    std::string sql = "SELECT name FROM permission_groups;";
    auto rows = db_->queryPrepared(sql, {}); // Empty params
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}

bool PermissionManager::addPermissionToGroup(const std::string& groupName, const std::string& permissionName) {
    std::string gid = getIdByName("permission_groups", groupName);
    std::string pid = getIdByName("permissions", permissionName);
    if (gid.empty() || pid.empty()) {
        ll::mod::NativeMod::current()->getLogger().warn("AddPermissionToGroup: Group '%s' or Permission '%s' not found.", groupName.c_str(), permissionName.c_str());
        return false;
    }
    std::string sql = "INSERT OR IGNORE INTO group_permissions (group_id, permission_id) VALUES (?, ?);";
    return db_->executePrepared(sql, {gid, pid});
}

bool PermissionManager::removePermissionFromGroup(const std::string& groupName, const std::string& permissionName) {
    std::string gid = getIdByName("permission_groups", groupName);
    std::string pid = getIdByName("permissions", permissionName);
     if (gid.empty() || pid.empty()) {
        // Don't warn if trying to remove non-existent mapping
        return false; // Or true, depending on desired semantics (idempotency)
    }
    std::string sql = "DELETE FROM group_permissions WHERE group_id = ? AND permission_id = ?;";
    return db_->executePrepared(sql, {gid, pid});
}

std::vector<std::string> PermissionManager::getParentGroups(const std::string& groupName) {
    std::vector<std::string> parents;
    std::string sql = "SELECT pg2.name FROM permission_groups pg1 "
                      "JOIN group_inheritance gi ON pg1.id = gi.group_id "
                      "JOIN permission_groups pg2 ON gi.parent_group_id = pg2.id "
                      "WHERE pg1.name = ?;";
    auto rows = db_->queryPrepared(sql, {groupName});
    for (auto& row : rows) if (!row.empty()) parents.push_back(row[0]);
    return parents;
}

bool PermissionManager::addGroupInheritance(const std::string& groupName, const std::string& parentGroupName) {
    std::string gid = getIdByName("permission_groups", groupName);
    std::string pgid = getIdByName("permission_groups", parentGroupName);
    if (gid.empty() || pgid.empty()) {
         ll::mod::NativeMod::current()->getLogger().warn("AddGroupInheritance: Group '%s' or Parent Group '%s' not found.", groupName.c_str(), parentGroupName.c_str());
        return false;
    }
    if (gid == pgid) { // Prevent self-inheritance
         ll::mod::NativeMod::current()->getLogger().warn("AddGroupInheritance: Cannot make group '%s' inherit from itself.", groupName.c_str());
        return false;
    }
    // TODO: Add cycle detection?
    std::string sql = "INSERT OR IGNORE INTO group_inheritance (group_id, parent_group_id) VALUES (?, ?);";
    return db_->executePrepared(sql, {gid, pgid});
}

bool PermissionManager::removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName) {
    std::string gid = getIdByName("permission_groups", groupName);
    std::string pgid = getIdByName("permission_groups", parentGroupName);
    if (gid.empty() || pgid.empty()) {
        return false; // Don't warn if trying to remove non-existent mapping
    }
    std::string sql = "DELETE FROM group_inheritance WHERE group_id = ? AND parent_group_id = ?;";
    return db_->executePrepared(sql, {gid, pgid});
}

std::vector<std::string> PermissionManager::getPermissionsOfGroup(const std::string& groupName) {
    std::vector<std::string> perms;
    std::set<std::string> visited; // Prevent cycles in inheritance
    std::function<void(const std::string&)> dfs =
        [&](const std::string& currentGroupName) {
        if (visited.count(currentGroupName)) return;
        visited.insert(currentGroupName);

        // Get direct permissions for the current group
        std::string directPermsSql = "SELECT p.name FROM permissions p "
                                     "JOIN group_permissions gp ON p.id = gp.permission_id "
                                     "JOIN permission_groups pg ON gp.group_id = pg.id "
                                     "WHERE pg.name = ?;";
        auto directRows = db_->queryPrepared(directPermsSql, {currentGroupName});
        for (auto& row : directRows) {
            if (!row.empty()) {
                perms.push_back(row[0]);
            }
        }

        // Recursively get permissions from parent groups
        auto parentGroups = getParentGroups(currentGroupName); // This now uses prepared statements
        for (const auto& parentGroup : parentGroups) {
            dfs(parentGroup);
        }
    };

    dfs(groupName);
    // Optional: Remove duplicate permissions if inheritance causes overlap
    std::sort(perms.begin(), perms.end());
    perms.erase(std::unique(perms.begin(), perms.end()), perms.end());
    return perms;
}


bool PermissionManager::addPlayerToGroup(const std::string& playerUuid, const std::string& groupName) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Adding player '%s' to group '%s'", playerUuid.c_str(), groupName.c_str());
    std::string gid = getIdByName("permission_groups", groupName);
    if (gid.empty()) {
        logger.warn("AddPlayerToGroup: Group '%s' not found.", groupName.c_str());
        return false;
    }
    std::string sql = "INSERT OR IGNORE INTO player_groups (player_uuid, group_id) VALUES (?, ?);";
    return db_->executePrepared(sql, {playerUuid, gid});
}

bool PermissionManager::removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Removing player '%s' from group '%s'", playerUuid.c_str(), groupName.c_str());
    std::string gid = getIdByName("permission_groups", groupName);
     if (gid.empty()) {
        // Don't warn if group doesn't exist when trying to remove
        return false;
    }
    std::string sql = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";
    return db_->executePrepared(sql, {playerUuid, gid});
}

std::vector<std::string> PermissionManager::getPlayerGroups(const std::string& playerUuid) {
    std::vector<std::string> list;
    std::string sql = "SELECT pg.name FROM permission_groups pg "
                      "JOIN player_groups pgp ON pg.id = pgp.group_id "
                      "WHERE pgp.player_uuid = ?;";
    auto rows = db_->queryPrepared(sql, {playerUuid});
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}

std::vector<std::string> PermissionManager::getPlayersInGroup(const std::string& groupName) {
    std::vector<std::string> list;
    std::string gid = getIdByName("permission_groups", groupName);
    if (gid.empty()) {
        // Group doesn't exist, return empty list
        return list;
    }
    std::string sql = "SELECT player_uuid FROM player_groups WHERE group_id = ?;";
    auto rows = db_->queryPrepared(sql, {gid});
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}

bool PermissionManager::setGroupPriority(const std::string& groupName, int priority) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("Setting priority %d for group '%s'", priority, groupName.c_str());
    // Check existence using prepared statement for consistency, though not strictly necessary before UPDATE
    if (!groupExists(groupName)) {
         logger.warn("SetGroupPriority: Group '%s' not found.", groupName.c_str());
         return false;
    }
    std::string sql = "UPDATE permission_groups SET priority = ? WHERE name = ?;";
    return db_->executePrepared(sql, {std::to_string(priority), groupName});
}

int PermissionManager::getGroupPriority(const std::string& groupName) {
    std::string sql = "SELECT priority FROM permission_groups WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return 0; // Default priority 0 if not found or error
    try {
        return std::stoi(rows[0][0]);
    } catch (const std::invalid_argument& ia) {
        ll::mod::NativeMod::current()->getLogger().error("Invalid priority value for group '%s': %s", groupName.c_str(), rows[0][0].c_str());
    } catch (const std::out_of_range& oor) {
         ll::mod::NativeMod::current()->getLogger().error("Priority value out of range for group '%s': %s", groupName.c_str(), rows[0][0].c_str());
    }
    return 0; // Return default on parsing error
}

bool PermissionManager::hasPermission(const std::string& playerUuid, const std::string& permissionNode) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("Checking permission '%s' for player '%s'", permissionNode.c_str(), playerUuid.c_str());

    // getPlayerGroups, getGroupPriority, getPermissionsOfGroup now use prepared statements internally
    auto groups = getPlayerGroups(playerUuid);

    if (!groups.empty()) {
        struct GroupInfo { std::string name; int priority; };
        std::vector<GroupInfo> playerGroupInfos;
        playerGroupInfos.reserve(groups.size());
        for (const auto& groupName : groups) {
            playerGroupInfos.push_back({groupName, getGroupPriority(groupName)});
        }

        // Sort groups by priority (descending)
        std::sort(playerGroupInfos.begin(), playerGroupInfos.end(),
                  [](const GroupInfo& a, const GroupInfo& b) {
                      return a.priority > b.priority;
                  });

        for (const auto& groupInfo : playerGroupInfos) {
            // getPermissionsOfGroup already handles inheritance
            auto groupPermissions = getPermissionsOfGroup(groupInfo.name);
            for (const auto& rule : groupPermissions) {
                bool isNegated = false;
                std::string permissionPattern = rule;
                if (!permissionPattern.empty() && permissionPattern[0] == '-') {
                    isNegated = true;
                    permissionPattern = permissionPattern.substr(1);
                }

                // Convert wildcard pattern to regex
                std::string regexPatternStr = "^";
                for (char c : permissionPattern) {
                    if (c == '*') {
                        regexPatternStr += ".*";
                    } else if (std::string(".\\+?^$[](){}|").find(c) != std::string::npos) {
                        regexPatternStr += '\\'; // Escape regex special characters
                        regexPatternStr += c;
                    } else {
                        regexPatternStr += c;
                    }
                }
                regexPatternStr += "$";

                try {
                    std::regex permissionRegex(regexPatternStr);
                    if (std::regex_match(permissionNode, permissionRegex)) {
                        logger.debug("Permission '%s' %s by rule '%s' in group '%s' (priority %d)",
                                     permissionNode.c_str(),
                                     isNegated ? "denied" : "granted",
                                     rule.c_str(),
                                     groupInfo.name.c_str(),
                                     groupInfo.priority);
                        return !isNegated; // Explicit rule found, return immediately
                    }
                } catch (const std::regex_error& e) {
                     logger.error("Invalid regex pattern generated from rule '%s': %s", rule.c_str(), e.what());
                     // Skip this invalid rule
                }
            }
        }
        logger.debug("Permission '%s' not explicitly matched in player's groups.", permissionNode.c_str());
    } else {
        logger.debug("Player '%s' belongs to no groups.", playerUuid.c_str());
    }

    // Check the permission's default value if no group rule matched
    std::string defaultSql = "SELECT default_value FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(defaultSql, {permissionNode});

    if (!rows.empty() && !rows[0].empty()) {
        try {
            bool defaultValue = std::stoi(rows[0][0]) != 0;
            logger.debug("Permission '%s' using default value: %s", permissionNode.c_str(), defaultValue ? "true" : "false");
            return defaultValue;
        } catch (const std::invalid_argument& ia) {
             logger.error("Invalid default_value for permission '%s': %s", permissionNode.c_str(), rows[0][0].c_str());
        } catch (const std::out_of_range& oor) {
             logger.error("Default_value out of range for permission '%s': %s", permissionNode.c_str(), rows[0][0].c_str());
        }
    } else {
         logger.debug("Permission node '%s' not found in permissions table.", permissionNode.c_str());
    }

    // Default deny if permission node doesn't exist or default value is invalid/missing
    logger.debug("Permission '%s' denied (not found or no applicable rule/default).", permissionNode.c_str());
    return false;
}

} // namespace permission
} // namespace BA // Changed from my_mod
