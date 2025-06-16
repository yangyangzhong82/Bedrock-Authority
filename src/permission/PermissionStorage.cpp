#include "permission/PermissionStorage.h"
#include "db/IDatabase.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"

namespace BA {
namespace permission {
namespace internal {

// Removed 'using namespace std;' to explicitly qualify standard library types.
// using namespace std;

PermissionStorage::PermissionStorage(db::IDatabase* db) : m_db(db) {}

bool PermissionStorage::ensureTables() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("Storage: Ensuring database tables exist...");

    auto executeAndLog = [&](const std::string& sql, const std::string& description) {
        if (!m_db) return false;
        bool success = m_db->execute(sql);
        logger.debug(
            "Storage: For '{}', executed SQL: '{}'. Result: {}",
            description,
            sql,
            success ? "Success" : "Failure"
        );
        return success;
    };

    if (!m_db) {
        logger.error("Storage: Database is not initialized.");
        return false;
    }

    executeAndLog(
        m_db->getCreateTableSql(
            "permissions",
            "id " + m_db->getAutoIncrementPrimaryKeyDefinition()
                + ", "
                  "name VARCHAR(255) UNIQUE NOT NULL, "
                  "description TEXT, "
                  "default_value INT NOT NULL DEFAULT 0"
        ),
        "Create permissions table"
    );

    executeAndLog(
        m_db->getCreateTableSql(
            "permission_groups",
            "id " + m_db->getAutoIncrementPrimaryKeyDefinition()
                + ", "
                  "name VARCHAR(255) UNIQUE NOT NULL, "
                  "description TEXT, "
                  "priority INT NOT NULL DEFAULT 0"
        ), // Add priority here directly
        "Create permission_groups table"
    );

    executeAndLog(
        m_db->getCreateTableSql(
            "group_permissions",
            "group_id INT NOT NULL, "
            "permission_rule VARCHAR(255) NOT NULL, "
            "PRIMARY KEY (group_id, permission_rule), "
            "FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE"
        ),
        "Create group_permissions table"
    );
    executeAndLog(
        m_db->getCreateTableSql(
            "group_inheritance",
            "group_id INT NOT NULL, "
            "parent_group_id INT NOT NULL, "
            "PRIMARY KEY (group_id, parent_group_id), "
            "FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE, "
            "FOREIGN KEY (parent_group_id) REFERENCES permission_groups(id) ON DELETE CASCADE"
        ),
        "Create group_inheritance table"
    );
    executeAndLog(
        m_db->getCreateTableSql(
            "player_groups",
            "player_uuid VARCHAR(36) NOT NULL, "
            "group_id INT NOT NULL, "
            "PRIMARY KEY (player_uuid, group_id), "
            "FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE"
        ),
        "Create player_groups table"
    );

    // Indexes
    executeAndLog(
        m_db->getCreateIndexSql("idx_permissions_name", "permissions", "name"),
        "Create index on permissions.name"
    );
    executeAndLog(
        m_db->getCreateIndexSql("idx_permission_groups_name", "permission_groups", "name"),
        "Create index on permission_groups.name"
    );
    executeAndLog(
        m_db->getCreateIndexSql("idx_player_groups_uuid", "player_groups", "player_uuid"),
        "Create index on player_groups.player_uuid"
    );

    logger.debug("Storage: Finished ensuring tables.");
    return true;
}

// --- Permissions ---
bool PermissionStorage::upsertPermission(const std::string& name, const std::string& description, bool defaultValue) {
    if (!m_db) return false;
    std::string defaultValueStr = defaultValue ? "1" : "0";
    std::string insertSql = m_db->getInsertOrIgnoreSql("permissions", "name, description, default_value", "?, ?, ?", "name");
    if (!m_db->executePrepared(insertSql, {name, description, defaultValueStr})) {
        return false;
    }
    std::string updateSql = "UPDATE permissions SET description = ?, default_value = ? WHERE name = ?;";
    return m_db->executePrepared(updateSql, {description, defaultValueStr, name});
}

bool PermissionStorage::permissionExists(const std::string& name) {
    if (!m_db) return false;
    std::string sql = "SELECT 1 FROM permissions WHERE name = ? LIMIT 1;";
    return !m_db->queryPrepared(sql, {name}).empty();
}

std::vector<std::string> PermissionStorage::fetchAllPermissionNames() {
    if (!m_db) return {};
    std::vector<std::string> list;
    std::string         sql  = "SELECT name FROM permissions;";
    auto           rows = m_db->queryPrepared(sql, {});
    for (auto& row : rows)
        if (!row.empty()) list.push_back(row[0]);
    return list;
}

std::vector<std::string> PermissionStorage::fetchDefaultPermissionNames() {
    if (!m_db) return {};
    std::vector<std::string> list;
    std::string         sql  = "SELECT name FROM permissions WHERE default_value = 1;";
    auto           rows = m_db->queryPrepared(sql, {});
    for (const auto& row : rows)
        if (!row.empty()) list.push_back(row[0]);
    return list;
}

std::unordered_map<std::string, bool> PermissionStorage::fetchAllPermissionDefaults() {
    if (!m_db) return {};
    std::unordered_map<std::string, bool> defaults;
    std::string                      sql  = "SELECT name, default_value FROM permissions;";
    auto                        rows = m_db->queryPrepared(sql, {});
    for (const auto& row : rows) {
        if (row.size() >= 2 && !row[0].empty() && !row[1].empty()) {
            try {
                defaults[row[0]] = (std::stoi(row[1]) != 0);
            } catch (const std::exception& e) {
                ::ll::mod::NativeMod::current()->getLogger().warn(
                    "PermissionStorage: Failed to convert default_value '{}' for permission '{}' to int: {}",
                    row[1], row[0], e.what()
                );
            }
        }
    }
    return defaults;
}

// --- Groups ---
bool PermissionStorage::createGroup(
    const std::string& groupName,
    const std::string& description,
    std::string&       outGroupId
) {
    if (!m_db) return false;
    std::string insertSql = m_db->getInsertOrIgnoreSql("permission_groups", "name, description", "?, ?", "name");
    m_db->executePrepared(insertSql, {groupName, description}); // Ignore result, just attempt.
    outGroupId = fetchGroupIdByName(groupName);
    return !outGroupId.empty();
}

bool PermissionStorage::deleteGroup(const std::string& groupId) {
    if (!m_db) return false;
    // ON DELETE CASCADE in FKs will handle related table cleanups.
    std::string sql = "DELETE FROM permission_groups WHERE id = ?;";
    return m_db->executePrepared(sql, {groupId});
}

std::string PermissionStorage::fetchGroupIdByName(const std::string& groupName) {
    if (!m_db) return "";
    std::string sql  = "SELECT id FROM permission_groups WHERE name = ? LIMIT 1;";
    auto   rows = m_db->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return "";
    return rows[0][0];
}

std::vector<std::string> PermissionStorage::fetchAllGroupNames() {
    if (!m_db) return {};
    std::vector<std::string> list;
    std::string         sql  = "SELECT name FROM permission_groups;";
    auto           rows = m_db->queryPrepared(sql, {});
    for (auto& row : rows)
        if (!row.empty()) list.push_back(row[0]);
    return list;
}

bool PermissionStorage::groupExists(const std::string& groupName) {
    if (!m_db) return false;
    std::string sql = "SELECT 1 FROM permission_groups WHERE name = ? LIMIT 1;";
    return !m_db->queryPrepared(sql, {groupName}).empty();
}

GroupDetails PermissionStorage::fetchGroupDetails(const std::string& groupName) {
    if (!m_db) return {};
    std::string sql  = "SELECT id, name, description, priority FROM permission_groups WHERE name = ? LIMIT 1;";
    auto   rows = m_db->queryPrepared(sql, {groupName});

    if (!rows.empty() && rows[0].size() >= 4) {
        try {
            int priority = std::stoi(rows[0][3]);
            return GroupDetails(rows[0][0], rows[0][1], rows[0][2], priority);
        } catch (const std::exception& e) {
            ::ll::mod::NativeMod::current()->getLogger().warn(
                "PermissionStorage: Failed to convert priority '{}' for group '{}' to int: {}",
                rows[0][3], groupName, e.what()
            );
        }
    }
    return {}; // Returns invalid GroupDetails
}

int PermissionStorage::fetchGroupPriority(const std::string& groupName) {
    if (!m_db) return 0;
    std::string sql  = "SELECT priority FROM permission_groups WHERE name = ? LIMIT 1;";
    auto   rows = m_db->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return 0;
    try {
        return std::stoi(rows[0][0]);
    } catch (const std::exception& e) {
        ::ll::mod::NativeMod::current()->getLogger().warn(
            "PermissionStorage: Failed to convert priority '{}' for group '{}' to int: {}",
            rows[0][0], groupName, e.what()
        );
        return 0;
    }
}

bool PermissionStorage::updateGroupPriority(const std::string& groupName, int priority) {
    if (!m_db) return false;
    std::string sql = "UPDATE permission_groups SET priority = ? WHERE name = ?;";
    return m_db->executePrepared(sql, {std::to_string(priority), groupName});
}

bool PermissionStorage::updateGroupDescription(const std::string& groupName, const std::string& newDescription) {
    if (!m_db) return false;
    std::string sql = "UPDATE permission_groups SET description = ? WHERE name = ?;";
    return m_db->executePrepared(sql, {newDescription, groupName});
}

std::string PermissionStorage::fetchGroupDescription(const std::string& groupName) {
    if (!m_db) return "";
    std::string sql  = "SELECT description FROM permission_groups WHERE name = ? LIMIT 1;";
    auto   rows = m_db->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return "";
    return rows[0][0];
}

// --- Group Permissions ---
bool PermissionStorage::addPermissionToGroup(const std::string& groupId, const std::string& permissionRule) {
    if (!m_db) return false;
    std::string insertSql = m_db->getInsertOrIgnoreSql(
        "group_permissions",
        "group_id, permission_rule",
        "?, ?",
        "group_id, permission_rule"
    );
    return m_db->executePrepared(insertSql, {groupId, permissionRule});
}

bool PermissionStorage::removePermissionFromGroup(const std::string& groupId, const std::string& permissionRule) {
    if (!m_db) return false;
    std::string sql = "DELETE FROM group_permissions WHERE group_id = ? AND permission_rule = ?;";
    return m_db->executePrepared(sql, {groupId, permissionRule});
}

std::vector<std::string> PermissionStorage::fetchDirectPermissionsOfGroup(const std::string& groupId) {
    if (!m_db) return {};
    std::vector<std::string> perms;
    std::string         sql  = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
    auto           rows = m_db->queryPrepared(sql, {groupId});
    for (auto& row : rows)
        if (!row.empty()) perms.push_back(row[0]);
    return perms;
}

// --- Inheritance ---
bool PermissionStorage::addGroupInheritance(const std::string& groupId, const std::string& parentGroupId) {
    if (!m_db) return false;
    std::string insertSql = m_db->getInsertOrIgnoreSql(
        "group_inheritance",
        "group_id, parent_group_id",
        "?, ?",
        "group_id, parent_group_id"
    );
    return m_db->executePrepared(insertSql, {groupId, parentGroupId});
}

bool PermissionStorage::removeGroupInheritance(const std::string& groupId, const std::string& parentGroupId) {
    if (!m_db) return false;
    std::string sql = "DELETE FROM group_inheritance WHERE group_id = ? AND parent_group_id = ?;";
    return m_db->executePrepared(sql, {groupId, parentGroupId});
}

std::unordered_map<std::string, std::set<std::string>> PermissionStorage::fetchAllInheritance() {
    if (!m_db) return {};
    std::unordered_map<std::string, std::set<std::string>> parentToChildren;
    std::string                             sql  = "SELECT T1.name AS child_name, T2.name AS parent_name "
                                              "FROM group_inheritance gi "
                                              "JOIN permission_groups T1 ON gi.group_id = T1.id "
                                              "JOIN permission_groups T2 ON gi.parent_group_id = T2.id;";
    auto                               rows = m_db->queryPrepared(sql, {});
    for (const auto& row : rows) {
        if (row.size() >= 2 && !row[0].empty() && !row[1].empty()) {
            parentToChildren[row[1]].insert(row[0]); // parent -> set<child>
        }
    }
    return parentToChildren;
}


// --- Player Groups ---
bool PermissionStorage::addPlayerToGroup(const std::string& playerUuid, const std::string& groupId) {
    if (!m_db) return false;
    std::string insertSql =
        m_db->getInsertOrIgnoreSql("player_groups", "player_uuid, group_id", "?, ?", "player_uuid, group_id");
    return m_db->executePrepared(insertSql, {playerUuid, groupId});
}

bool PermissionStorage::removePlayerFromGroup(const std::string& playerUuid, const std::string& groupId) {
    if (!m_db) return false;
    std::string sql = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";
    return m_db->executePrepared(sql, {playerUuid, groupId});
}

std::vector<GroupDetails> PermissionStorage::fetchPlayerGroupsWithDetails(const std::string& playerUuid) {
    if (!m_db) return {};
    std::vector<GroupDetails> playerGroupDetails;
    std::string               sql  = "SELECT pg.id, pg.name, pg.description, pg.priority "
                                     "FROM permission_groups pg "
                                     "JOIN player_groups pgr ON pg.id = pgr.group_id "
                                     "WHERE pgr.player_uuid = ?;";
    auto                      rows = m_db->queryPrepared(sql, {playerUuid});
    for (const auto& row : rows) {
        if (row.size() >= 4) {
            try {
                playerGroupDetails.emplace_back(row[0], row[1], row[2], std::stoi(row[3]));
            } catch (const std::exception& e) {
                ::ll::mod::NativeMod::current()->getLogger().warn(
                    "PermissionStorage: Failed to convert priority '{}' for player group '{}' to int: {}",
                    row[3], row[1], e.what()
                );
            }
        }
    }
    return playerGroupDetails;
}

std::vector<std::string> PermissionStorage::fetchPlayersInGroup(const std::string& groupId) {
    if (!m_db) return {};
    std::vector<std::string> players;
    std::string         sql  = "SELECT player_uuid FROM player_groups WHERE group_id = ?;";
    auto           rows = m_db->queryPrepared(sql, {groupId});
    for (auto& row : rows)
        if (!row.empty()) players.push_back(row[0]);
    return players;
}

std::vector<std::string> PermissionStorage::fetchPlayersInGroups(const std::vector<std::string>& groupIds) {
    if (!m_db || groupIds.empty()) return {};
    std::vector<std::string> players;
    std::string         placeholders = m_db->getInClausePlaceholders(groupIds.size());
    std::string         sql          = "SELECT DISTINCT player_uuid FROM player_groups WHERE group_id IN (" + placeholders + ");";
    auto           rows         = m_db->queryPrepared(sql, groupIds);
    for (auto& row : rows)
        if (!row.empty()) players.push_back(row[0]);
    return players;
}

std::unordered_map<std::string, std::string> PermissionStorage::fetchGroupIdsByNames(const std::set<std::string>& groupNames) {
    if (!m_db || groupNames.empty()) return {};
    std::unordered_map<std::string, std::string> groupNameMap;
    std::vector<std::string>                namesVec(groupNames.begin(), groupNames.end());
    std::string                        placeholders = m_db->getInClausePlaceholders(namesVec.size());
    std::string                        sql          = "SELECT name, id FROM permission_groups WHERE name IN (" + placeholders + ");";
    auto                          rows         = m_db->queryPrepared(sql, namesVec);
    for (const auto& row : rows) {
        if (row.size() >= 2 && !row[0].empty() && !row[1].empty()) {
            groupNameMap[row[0]] = row[1];
        }
    }
    return groupNameMap;
}

// --- Bulk Operations ---
size_t
PermissionStorage::addPermissionsToGroup(const std::string& groupId, const std::vector<std::string>& permissionRules) {
    if (!m_db || permissionRules.empty()) return 0;
    if (!m_db->beginTransaction()) return 0;

    size_t successCount = 0;
    std::string insertSql    = m_db->getInsertOrIgnoreSql(
        "group_permissions",
        "group_id, permission_rule",
        "?, ?",
        "group_id, permission_rule"
    );

    for (const auto& rule : permissionRules) {
        if (rule.empty() || rule == "-") continue;
        if (m_db->executePrepared(insertSql, {groupId, rule})) {
            successCount++;
        }
    }

    if (!m_db->commit()) {
        m_db->rollback();
        return 0;
    }
    return successCount;
}

size_t PermissionStorage::removePermissionsFromGroup(
    const std::string&              groupId,
    const std::vector<std::string>& permissionRules
) {
    if (!m_db || permissionRules.empty()) return 0;
    if (!m_db->beginTransaction()) return 0;

    size_t successCount = 0;
    std::string deleteSql    = "DELETE FROM group_permissions WHERE group_id = ? AND permission_rule = ?;";

    for (const auto& rule : permissionRules) {
        if (rule.empty() || rule == "-") continue;
        if (m_db->executePrepared(deleteSql, {groupId, rule})) {
            successCount++;
        }
    }

    if (!m_db->commit()) {
        m_db->rollback();
        return 0;
    }
    return successCount;
}

size_t PermissionStorage::addPlayerToGroups(const std::string& playerUuid, const std::vector<std::pair<std::string, std::string>>& groupInfos) {
    if (!m_db || groupInfos.empty()) return 0;
    if (!m_db->beginTransaction()) return 0;

    size_t successCount = 0;
    std::string insertSql =
        m_db->getInsertOrIgnoreSql("player_groups", "player_uuid, group_id", "?, ?", "player_uuid, group_id");

    for (const auto& groupInfo : groupInfos) {
        // Here we assume the second element is the groupId
        if (m_db->executePrepared(insertSql, {playerUuid, groupInfo.second})) {
            successCount++;
        }
    }

    if (!m_db->commit()) {
        m_db->rollback();
        return 0;
    }
    return successCount;
}

size_t
PermissionStorage::removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupIds) {
    if (!m_db || groupIds.empty()) return 0;
    if (!m_db->beginTransaction()) return 0;

    size_t successCount = 0;
    std::string deleteSql    = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";

    for (const auto& groupId : groupIds) {
        if (m_db->executePrepared(deleteSql, {playerUuid, groupId})) {
            successCount++;
        }
    }

    if (!m_db->commit()) {
        m_db->rollback();
        return 0;
    }
    return successCount;
}

std::vector<std::string> PermissionStorage::fetchDirectParentGroupIds(const std::string& groupId) {
    if (!m_db) return {};
    std::vector<std::string> parentIds;
    std::string         sql  = "SELECT parent_group_id FROM group_inheritance WHERE group_id = ?;";
    auto           rows = m_db->queryPrepared(sql, {groupId});
    for (auto& row : rows)
        if (!row.empty()) parentIds.push_back(row[0]);
    return parentIds;
}

std::unordered_map<std::string, std::string> PermissionStorage::fetchGroupNamesByIds(const std::vector<std::string>& groupIds) {
    if (!m_db || groupIds.empty()) return {};
    std::unordered_map<std::string, std::string> groupNameMap;
    std::string                        placeholders = m_db->getInClausePlaceholders(groupIds.size());
    std::string                        sql          = "SELECT id, name FROM permission_groups WHERE id IN (" + placeholders + ");";
    auto                          rows         = m_db->queryPrepared(sql, groupIds);
    for (const auto& row : rows) {
        if (row.size() >= 2 && !row[0].empty() && !row[1].empty()) {
            groupNameMap[row[0]] = row[1];
        }
    }
    return groupNameMap;
}

} // namespace internal
} // namespace permission
} // namespace BA
