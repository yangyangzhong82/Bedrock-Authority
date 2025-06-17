#include "permission/PermissionStorage.h"
#include "db/IDatabase.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include <string> // 显式包含 string
#include <vector> // 显式包含 vector
#include <unordered_map> // 显式包含 unordered_map
#include <set> // 显式包含 set
#include <stdexcept> // 显式包含 stdexcept 用于 std::exception

namespace BA {
namespace permission {
namespace internal {

// 移除了 'using namespace std;' 以显式限定标准库类型。

/**
 * @brief 权限存储类的构造函数。
 * @param db 数据库接口指针。
 */
PermissionStorage::PermissionStorage(db::IDatabase* db) : m_db(db) {}

/**
 * @brief 确保所有必要的数据库表都已创建。
 * @return 如果所有表都已成功创建或存在，则返回 true；否则返回 false。
 */
bool PermissionStorage::ensureTables() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("存储: 正在确保数据库表存在...");

    auto executeAndLog = [&](const std::string& sql, const std::string& description) {
        if (!m_db) return false;
        bool success = m_db->execute(sql);
        logger.debug(
            "存储: 对于 '{}', 执行 SQL: '{}'. 结果: {}",
            description,
            sql,
            success ? "成功" : "失败"
        );
        return success;
    };

    if (!m_db) {
        logger.error("存储: 数据库未初始化。");
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
        "创建权限表"
    );

    executeAndLog(
        m_db->getCreateTableSql(
            "permission_groups",
            "id " + m_db->getAutoIncrementPrimaryKeyDefinition()
                + ", "
                  "name VARCHAR(255) UNIQUE NOT NULL, "
                  "description TEXT, "
                  "priority INT NOT NULL DEFAULT 0"
        ), // 直接在此处添加优先级字段
        "创建权限组表"
    );

    executeAndLog(
        m_db->getCreateTableSql(
            "group_permissions",
            "group_id INT NOT NULL, "
            "permission_rule VARCHAR(255) NOT NULL, "
            "PRIMARY KEY (group_id, permission_rule), "
            "FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE"
        ),
        "创建组权限表"
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
        "创建组继承表"
    );
    executeAndLog(
        m_db->getCreateTableSql(
            "player_groups",
            "player_uuid VARCHAR(36) NOT NULL, "
            "group_id INT NOT NULL, "
            "expiry_timestamp BIGINT NULL DEFAULT NULL, "
            "PRIMARY KEY (player_uuid, group_id), "
            "FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE"
        ),
        "创建玩家组表"
    );

    // 确保 player_groups 表有 expiry_timestamp 列
    executeAndLog(
        m_db->getAddColumnSql("player_groups", "expiry_timestamp", "BIGINT NULL DEFAULT NULL"),
        "为 player_groups 表添加 expiry_timestamp 列"
    );

    // 索引
    executeAndLog(
        m_db->getCreateIndexSql("idx_permissions_name", "permissions", "name"),
        "在 permissions.name 上创建索引"
    );
    executeAndLog(
        m_db->getCreateIndexSql("idx_permission_groups_name", "permission_groups", "name"),
        "在 permission_groups.name 上创建索引"
    );
    executeAndLog(
        m_db->getCreateIndexSql("idx_player_groups_uuid", "player_groups", "player_uuid"),
        "在 player_groups.player_uuid 上创建索引"
    );

    logger.debug("存储: 表格确保完成。");
    return true;
}

// --- 权限管理 ---
/**
 * @brief 插入或更新权限。
 * @param name 权限名称。
 * @param description 权限描述。
 * @param defaultValue 权限的默认值。
 * @return 如果操作成功，则返回 true；否则返回 false。
 */
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

/**
 * @brief 检查权限是否存在。
 * @param name 权限名称。
 * @return 如果权限存在，则返回 true；否则返回 false。
 */
bool PermissionStorage::permissionExists(const std::string& name) {
    if (!m_db) return false;
    std::string sql = "SELECT 1 FROM permissions WHERE name = ? LIMIT 1;";
    return !m_db->queryPrepared(sql, {name}).empty();
}

/**
 * @brief 获取所有权限的名称。
 * @return 包含所有权限名称的字符串向量。
 */
std::vector<std::string> PermissionStorage::fetchAllPermissionNames() {
    if (!m_db) return {};
    std::vector<std::string> list;
    std::string         sql  = "SELECT name FROM permissions;";
    auto           rows = m_db->queryPrepared(sql, {});
    for (auto& row : rows)
        if (!row.empty()) list.push_back(row[0]);
    return list;
}

/**
 * @brief 获取所有默认权限的名称。
 * @return 包含所有默认权限名称的字符串向量。
 */
std::vector<std::string> PermissionStorage::fetchDefaultPermissionNames() {
    if (!m_db) return {};
    std::vector<std::string> list;
    std::string         sql  = "SELECT name FROM permissions WHERE default_value = 1;";
    auto           rows = m_db->queryPrepared(sql, {});
    for (const auto& row : rows)
        if (!row.empty()) list.push_back(row[0]);
    return list;
}

/**
 * @brief 获取所有权限及其默认值。
 * @return 权限名称到其默认值的映射。
 */
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
                    "权限存储: 无法将权限 '{}' 的默认值 '{}' 转换为整数: {}",
                    row[0], row[1], e.what()
                );
            }
        }
    }
    return defaults;
}

// --- 用户组管理 ---
/**
 * @brief 创建用户组。
 * @param groupName 用户组名称。
 * @param description 用户组描述。
 * @param outGroupId 输出参数，返回创建的用户组ID。
 * @return 如果用户组创建成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::createGroup(
    const std::string& groupName,
    const std::string& description,
    std::string&       outGroupId
) {
    if (!m_db) return false;
    std::string insertSql = m_db->getInsertOrIgnoreSql("permission_groups", "name, description", "?, ?", "name");
    m_db->executePrepared(insertSql, {groupName, description}); // 忽略结果，只尝试执行。
    outGroupId = fetchGroupIdByName(groupName);
    return !outGroupId.empty();
}

/**
 * @brief 删除用户组。
 * @param groupId 用户组ID。
 * @return 如果用户组删除成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::deleteGroup(const std::string& groupId) {
    if (!m_db) return false;
    // 外键上的 ON DELETE CASCADE 将处理相关表的清理。
    std::string sql = "DELETE FROM permission_groups WHERE id = ?;";
    return m_db->executePrepared(sql, {groupId});
}

/**
 * @brief 根据用户组名称获取用户组ID。
 * @param groupName 用户组名称。
 * @return 用户组ID，如果不存在则返回空字符串。
 */
std::string PermissionStorage::fetchGroupIdByName(const std::string& groupName) {
    if (!m_db) return "";
    std::string sql  = "SELECT id FROM permission_groups WHERE name = ? LIMIT 1;";
    auto   rows = m_db->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return "";
    return rows[0][0];
}

/**
 * @brief 获取所有用户组的名称。
 * @return 包含所有用户组名称的字符串向量。
 */
std::vector<std::string> PermissionStorage::fetchAllGroupNames() {
    if (!m_db) return {};
    std::vector<std::string> list;
    std::string         sql  = "SELECT name FROM permission_groups;";
    auto           rows = m_db->queryPrepared(sql, {});
    for (auto& row : rows)
        if (!row.empty()) list.push_back(row[0]);
    return list;
}

/**
 * @brief 检查用户组是否存在。
 * @param groupName 用户组名称。
 * @return 如果用户组存在，则返回 true；否则返回 false。
 */
bool PermissionStorage::groupExists(const std::string& groupName) {
    if (!m_db) return false;
    std::string sql = "SELECT 1 FROM permission_groups WHERE name = ? LIMIT 1;";
    return !m_db->queryPrepared(sql, {groupName}).empty();
}

/**
 * @brief 获取用户组的详细信息。
 * @param groupName 用户组名称。
 * @return 用户组详细信息对象。
 */
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
                "权限存储: 无法将组 '{}' 的优先级 '{}' 转换为整数: {}",
                groupName, rows[0][3], e.what()
            );
        }
    }
    return {}; // 返回无效的 GroupDetails
}

/**
 * @brief 获取用户组的优先级。
 * @param groupName 用户组名称。
 * @return 用户组优先级。
 */
int PermissionStorage::fetchGroupPriority(const std::string& groupName) {
    if (!m_db) return 0;
    std::string sql  = "SELECT priority FROM permission_groups WHERE name = ? LIMIT 1;";
    auto   rows = m_db->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return 0;
    try {
        return std::stoi(rows[0][0]);
    } catch (const std::exception& e) {
        ::ll::mod::NativeMod::current()->getLogger().warn(
            "权限存储: 无法将组 '{}' 的优先级 '{}' 转换为整数: {}",
            groupName, rows[0][0], e.what()
        );
        return 0;
    }
}

/**
 * @brief 更新用户组优先级。
 * @param groupName 用户组名称。
 * @param priority 新的优先级。
 * @return 如果更新成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::updateGroupPriority(const std::string& groupName, int priority) {
    if (!m_db) return false;
    std::string sql = "UPDATE permission_groups SET priority = ? WHERE name = ?;";
    return m_db->executePrepared(sql, {std::to_string(priority), groupName});
}

/**
 * @brief 更新用户组描述。
 * @param groupName 用户组名称。
 * @param newDescription 新的描述。
 * @return 如果更新成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::updateGroupDescription(const std::string& groupName, const std::string& newDescription) {
    if (!m_db) return false;
    std::string sql = "UPDATE permission_groups SET description = ? WHERE name = ?;";
    return m_db->executePrepared(sql, {newDescription, groupName});
}

/**
 * @brief 获取用户组描述。
 * @param groupName 用户组名称。
 * @return 用户组描述，如果不存在则返回空字符串。
 */
std::string PermissionStorage::fetchGroupDescription(const std::string& groupName) {
    if (!m_db) return "";
    std::string sql  = "SELECT description FROM permission_groups WHERE name = ? LIMIT 1;";
    auto   rows = m_db->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return "";
    return rows[0][0];
}

// --- 用户组权限管理 ---
/**
 * @brief 为用户组添加权限规则。
 * @param groupId 用户组ID。
 * @param permissionRule 权限规则。
 * @return 如果添加成功，则返回 true；否则返回 false。
 */
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

/**
 * @brief 从用户组中移除权限规则。
 * @param groupId 用户组ID。
 * @param permissionRule 权限规则。
 * @return 如果移除成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::removePermissionFromGroup(const std::string& groupId, const std::string& permissionRule) {
    if (!m_db) return false;
    std::string sql = "DELETE FROM group_permissions WHERE group_id = ? AND permission_rule = ?;";
    return m_db->executePrepared(sql, {groupId, permissionRule});
}

/**
 * @brief 获取用户组的直接权限。
 * @param groupId 用户组ID。
 * @return 包含用户组直接权限规则的字符串向量。
 */
std::vector<std::string> PermissionStorage::fetchDirectPermissionsOfGroup(const std::string& groupId) {
    if (!m_db) return {};
    std::vector<std::string> perms;
    std::string         sql  = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
    auto           rows = m_db->queryPrepared(sql, {groupId});
    for (auto& row : rows)
        if (!row.empty()) perms.push_back(row[0]);
    return perms;
}

// --- 继承关系管理 ---
/**
 * @brief 添加用户组继承关系。
 * @param groupId 子用户组ID。
 * @param parentGroupId 父用户组ID。
 * @return 如果添加成功，则返回 true；否则返回 false。
 */
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

/**
 * @brief 移除用户组继承关系。
 * @param groupId 子用户组ID。
 * @param parentGroupId 父用户组ID。
 * @return 如果移除成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::removeGroupInheritance(const std::string& groupId, const std::string& parentGroupId) {
    if (!m_db) return false;
    std::string sql = "DELETE FROM group_inheritance WHERE group_id = ? AND parent_group_id = ?;";
    return m_db->executePrepared(sql, {groupId, parentGroupId});
}

/**
 * @brief 获取所有继承关系。
 * @return 父用户组ID到其子用户组ID集合的映射。
 */
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


// --- 玩家用户组管理 ---
/**
 * @brief 将玩家添加到用户组。
 * @param playerUuid 玩家UUID。
 * @param groupId 用户组ID。
 * @return 如果添加成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::addPlayerToGroup(const std::string& playerUuid, const std::string& groupId) {
    return addPlayerToGroup(playerUuid, groupId, std::nullopt);
}

bool PermissionStorage::addPlayerToGroup(
    const std::string&              playerUuid,
    const std::string&              groupId,
    const std::optional<long long>& expiryTimestamp
) {
    if (!m_db) return false;

    // 这是一个通用的“插入或更新”逻辑
    // 1. 先尝试删除，确保我们是从一个干净的状态开始
    std::string deleteSql = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";
    m_db->executePrepared(deleteSql, {playerUuid, groupId}); // 忽略结果

    // 2. 插入新记录
    std::string insertSql = "INSERT INTO player_groups (player_uuid, group_id, expiry_timestamp) VALUES (?, ?, ?);";
    if (expiryTimestamp.has_value()) {
        return m_db->executePrepared(insertSql, {playerUuid, groupId, std::to_string(*expiryTimestamp)});
    } else {
        // 数据库驱动应该能处理 NULL
        return m_db->executePrepared(
            insertSql,
            {playerUuid, groupId, ""}
        ); // 假设空字符串被驱动解释为NULL，或使用特定于驱动的方法
    }
}

/**
 * @brief 将玩家从用户组中移除。
 * @param playerUuid 玩家UUID。
 * @param groupId 用户组ID。
 * @return 如果移除成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::removePlayerFromGroup(const std::string& playerUuid, const std::string& groupId) {
    if (!m_db) return false;
    std::string sql = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";
    return m_db->executePrepared(sql, {playerUuid, groupId});
}

/**
 * @brief 获取玩家所属的用户组及其详细信息。
 * @param playerUuid 玩家UUID。
 * @return 包含玩家所属用户组详细信息的向量。
 */
std::vector<GroupDetails> PermissionStorage::fetchPlayerGroupsWithDetails(const std::string& playerUuid) {
    if (!m_db) return {};
    std::vector<GroupDetails> playerGroupDetails;

    long long currentTime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // 修改SQL查询以选择 expiry_timestamp 并过滤过期的记录
    std::string sql =
        "SELECT pg.id, pg.name, pg.description, pg.priority, pgr.expiry_timestamp " // <-- 添加 pgr.expiry_timestamp
        "FROM permission_groups pg "
        "JOIN player_groups pgr ON pg.id = pgr.group_id "
        "WHERE pgr.player_uuid = ? AND (pgr.expiry_timestamp IS NULL OR pgr.expiry_timestamp > ?);";

    auto rows = m_db->queryPrepared(sql, {playerUuid, std::to_string(currentTime)});
    for (const auto& row : rows) {
        if (row.size() >= 5) { // <-- 检查5列
            try {
                int                      priority = std::stoi(row[3]);
                std::optional<long long> expirationTime;
                if (!row[4].empty()) { // 检查 expiry_timestamp 是否为 NULL
                    expirationTime = std::stoll(row[4]);
                }
                playerGroupDetails.emplace_back(row[0], row[1], row[2], priority, expirationTime);
            } catch (const std::exception& e) {
                ::ll::mod::NativeMod::current()->getLogger().warn(
                    "权限存储: 无法解析玩家组 '{}' 的详细信息: {}",
                    row[1],
                    e.what()
                );
            }
        }
    }
    return playerGroupDetails;
}

/**
 * @brief 获取指定用户组中的所有玩家UUID。
 * @param groupId 用户组ID。
 * @return 包含玩家UUID的字符串向量。
 */
std::vector<std::string> PermissionStorage::fetchPlayersInGroup(const std::string& groupId) {
    if (!m_db) return {};
    std::vector<std::string> players;
    std::string         sql  = "SELECT player_uuid FROM player_groups WHERE group_id = ?;";
    auto           rows = m_db->queryPrepared(sql, {groupId});
    for (auto& row : rows)
        if (!row.empty()) players.push_back(row[0]);
    return players;
}

/**
 * @brief 获取在多个用户组中的所有玩家UUID。
 * @param groupIds 用户组ID向量。
 * @return 包含玩家UUID的字符串向量。
 */
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

/**
 * @brief 根据用户组名称获取用户组ID。
 * @param groupNames 用户组名称集合。
 * @return 用户组名称到用户组ID的映射。
 */
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

/**
 * @brief 根据用户组名称获取用户组详细信息。
 * @param groupNames 用户组名称集合。
 * @return 用户组名称到用户组详细信息的映射。
 */
std::unordered_map<std::string, GroupDetails> PermissionStorage::fetchGroupDetailsByNames(const std::set<std::string>& groupNames) {
    if (!m_db || groupNames.empty()) return {};
    std::unordered_map<std::string, GroupDetails> groupDetailsMap;
    std::vector<std::string>                 namesVec(groupNames.begin(), groupNames.end());
    std::string                         placeholders = m_db->getInClausePlaceholders(namesVec.size());
    std::string                         sql          = "SELECT id, name, description, priority FROM permission_groups WHERE name IN (" + placeholders + ");";
    auto                           rows         = m_db->queryPrepared(sql, namesVec);

    for (const auto& row : rows) {
        if (row.size() >= 4 && !row[0].empty() && !row[1].empty() && !row[2].empty() && !row[3].empty()) {
            try {
                int priority = std::stoi(row[3]);
                groupDetailsMap[row[1]] = GroupDetails(row[0], row[1], row[2], priority);
            } catch (const std::exception& e) {
                ::ll::mod::NativeMod::current()->getLogger().warn(
                    "权限存储: 无法将组 '{}' 的优先级 '{}' 转换为整数: {}",
                    row[1], row[3], e.what()
                );
            }
        }
    }
    return groupDetailsMap;
}

// --- 批量操作 ---
/**
 * @brief 为用户组批量添加权限规则。
 * @param groupId 用户组ID。
 * @param permissionRules 权限规则向量。
 * @return 成功添加的权限数量。
 */
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

/**
 * @brief 从用户组中批量移除权限规则。
 * @param groupId 用户组ID。
 * @param permissionRules 权限规则向量。
 * @return 成功移除的权限数量。
 */
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

/**
 * @brief 将玩家批量添加到多个用户组。
 * @param playerUuid 玩家UUID。
 * @param groupInfos 包含用户组名称和ID的pair向量。
 * @return 成功添加的玩家用户组关联数量。
 */
size_t PermissionStorage::addPlayerToGroups(const std::string& playerUuid, const std::vector<std::pair<std::string, std::string>>& groupInfos) {
    if (!m_db || groupInfos.empty()) return 0;
    if (!m_db->beginTransaction()) return 0;

    size_t successCount = 0;
    std::string insertSql =
        m_db->getInsertOrIgnoreSql("player_groups", "player_uuid, group_id", "?, ?", "player_uuid, group_id");

    for (const auto& groupInfo : groupInfos) {
        // 这里我们假设第二个元素是 groupId
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

/**
 * @brief 将玩家从多个用户组中批量移除。
 * @param playerUuid 玩家UUID。
 * @param groupIds 用户组ID向量。
 * @return 成功移除的玩家用户组关联数量。
 */
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

/**
 * @brief 获取用户组的所有直接父用户组ID。
 * @param groupId 用户组ID。
 * @return 包含直接父用户组ID的字符串向量。
 */
std::vector<std::string> PermissionStorage::fetchDirectParentGroupIds(const std::string& groupId) {
    if (!m_db) return {};
    std::vector<std::string> parentIds;
    std::string         sql  = "SELECT parent_group_id FROM group_inheritance WHERE group_id = ?;";
    auto           rows = m_db->queryPrepared(sql, {groupId});
    for (auto& row : rows)
        if (!row.empty()) parentIds.push_back(row[0]);
    return parentIds;
}

/**
 * @brief 根据用户组ID获取用户组名称。
 * @param groupIds 用户组ID向量。
 * @return 用户组ID到用户组名称的映射。
 */
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

std::vector<std::string> PermissionStorage::deleteExpiredPlayerGroups() {
    if (!m_db) return {};

    long long currentTime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // 1. 查询所有过期的玩家 UUID
    std::vector<std::string> expiredPlayerUuids;
    std::string selectSql = "SELECT DISTINCT player_uuid FROM player_groups WHERE expiry_timestamp IS NOT NULL AND expiry_timestamp <= ?;";
    auto rows = m_db->queryPrepared(selectSql, {std::to_string(currentTime)});
    for (const auto& row : rows) {
        if (!row.empty()) {
            expiredPlayerUuids.push_back(row[0]);
        }
    }

    // 2. 删除过期的玩家组记录
    std::string deleteSql = "DELETE FROM player_groups WHERE expiry_timestamp IS NOT NULL AND expiry_timestamp <= ?;";
    if (m_db->executePrepared(deleteSql, {std::to_string(currentTime)})) {
        ::ll::mod::NativeMod::current()->getLogger().debug("已执行过期的玩家组清理，删除了 {} 条记录。", expiredPlayerUuids.size());
    } else {
        ::ll::mod::NativeMod::current()->getLogger().warn("执行过期的玩家组清理失败。");
    }

    return expiredPlayerUuids;
}

/**
 * @brief 获取玩家在特定用户组中的过期时间戳。
 * @param playerUuid 玩家UUID。
 * @param groupId 用户组ID。
 * @return 如果存在且有过期时间，则返回过期时间戳；如果永不过期或不存在，则返回 std::nullopt。
 */
std::optional<long long>
PermissionStorage::fetchPlayerGroupExpirationTime(const std::string& playerUuid, const std::string& groupId) {
    if (!m_db) return std::nullopt;
    std::string sql  = "SELECT expiry_timestamp FROM player_groups WHERE player_uuid = ? AND group_id = ? LIMIT 1;";
    auto        rows = m_db->queryPrepared(sql, {playerUuid, groupId});

    if (rows.empty() || rows[0].empty()) {
        // 玩家不在组中
        return std::nullopt;
    }
    if (rows[0][0].empty()) {
        // 在组中，但永不过期 (NULL)
        return std::nullopt;
    }

    try {
        return std::stoll(rows[0][0]); // 返回时间戳
    } catch (const std::exception& e) {
        ::ll::mod::NativeMod::current()->getLogger().error(
            "权限存储: 无法将玩家 '{}' 组ID '{}' 的过期时间 '{}' 转换为 long long: {}",
            playerUuid,
            groupId,
            rows[0][0],
            e.what()
        );
        return std::nullopt; // 错误情况
    }
}
/**
 * @brief 更新玩家在特定用户组中的过期时间戳。
 * @param playerUuid 玩家UUID。
 * @param groupId 用户组ID。
 * @param expiryTimestamp 新的过期时间戳。std::nullopt 表示永不过期。
 * @return 如果更新成功，则返回 true；否则返回 false。
 */
bool PermissionStorage::updatePlayerGroupExpirationTime(
    const std::string&              playerUuid,
    const std::string&              groupId,
    const std::optional<long long>& expiryTimestamp
) {
    if (!m_db) return false;
    std::string sql = "UPDATE player_groups SET expiry_timestamp = ? WHERE player_uuid = ? AND group_id = ?;";

    if (expiryTimestamp.has_value()) {
        return m_db->executePrepared(sql, {std::to_string(*expiryTimestamp), playerUuid, groupId});
    } else {
        // 数据库驱动应能处理 NULL，假设空字符串被解释为 NULL
        return m_db->executePrepared(sql, {"", playerUuid, groupId});
    }
}
} // namespace internal
} // namespace permission
} // namespace BA
