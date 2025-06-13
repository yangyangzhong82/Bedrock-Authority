#include "db/IDatabase.h" 
#include "permission/PermissionManager.h"
#include <set>
#include <shared_mutex> // Include for locks
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"
#include <algorithm>
#include <regex>
#include <map> // For getPermissionsOfGroup
#include <functional> // For getPermissionsOfGroup lambda

namespace BA { // Changed from my_mod
namespace permission {

PermissionManager& PermissionManager::getInstance() {
    static PermissionManager instance;
    return instance;
}

void PermissionManager::init(db::IDatabase* db) {
    db_ = db;
    ensureTables();
    populateGroupCache(); // 初始化时填充缓存
    ll::mod::NativeMod::current()->getLogger().info("权限管理器已初始化并填充了组缓存");
}

void PermissionManager::ensureTables() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("确保权限表存在...");

    auto executeAndLog = [&](const std::string& sql, const std::string& description) {
        bool success = db_->execute(sql);
        logger.info("为 '%s' 执行 SQL: %s. 结果: %s", description.c_str(), sql.c_str(), success ? "成功" : "失败");
        // 注意：ALTER TABLE ADD COLUMN 如果列已存在，在某些数据库（如 SQLite）上可能会返回 false 或抛出异常（取决于实现），但这不一定是关键错误。
        // CREATE TABLE IF NOT EXISTS 应该总是返回 true（除非有严重错误）。
        return success;
    };

    db::DatabaseType dbType = db_->getType();
    std::string createPermissionsTableSql;
    std::string createPermissionGroupsTableSql;

    if (dbType == db::DatabaseType::SQLite) {
        logger.info("使用 SQLite 语法创建表...");
        createPermissionsTableSql = "CREATE TABLE IF NOT EXISTS permissions (id INTEGER PRIMARY KEY, name VARCHAR(255) UNIQUE NOT NULL, description TEXT, default_value INT NOT NULL DEFAULT 0);";
        createPermissionGroupsTableSql = "CREATE TABLE IF NOT EXISTS permission_groups (id INTEGER PRIMARY KEY, name VARCHAR(255) UNIQUE NOT NULL, description TEXT);";
    } else if (dbType == db::DatabaseType::MySQL) {
        logger.info("使用 MySQL 语法创建表...");
        createPermissionsTableSql = "CREATE TABLE IF NOT EXISTS permissions (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(255) UNIQUE NOT NULL, description TEXT, default_value INT NOT NULL DEFAULT 0);";
        createPermissionGroupsTableSql = "CREATE TABLE IF NOT EXISTS permission_groups (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(255) UNIQUE NOT NULL, description TEXT);";
    } else if (dbType == db::DatabaseType::PostgreSQL) {
        logger.info("使用 PostgreSQL 语法创建表...");
        createPermissionsTableSql = "CREATE TABLE IF NOT EXISTS permissions (id SERIAL PRIMARY KEY, name VARCHAR(255) UNIQUE NOT NULL, description TEXT, default_value INT NOT NULL DEFAULT 0);";
        createPermissionGroupsTableSql = "CREATE TABLE IF NOT EXISTS permission_groups (id SERIAL PRIMARY KEY, name VARCHAR(255) UNIQUE NOT NULL, description TEXT);";
    } else {
        logger.error("未知数据库类型，无法创建表。");
        return; // Exit if database type is unknown
    }

    executeAndLog(createPermissionsTableSql, "创建 permissions 表");
    executeAndLog(createPermissionGroupsTableSql, "创建 permission_groups 表");

    // group_permissions, group_inheritance, player_groups 表的语法是通用的
    executeAndLog("CREATE TABLE IF NOT EXISTS group_permissions (group_id INT NOT NULL, permission_rule VARCHAR(255) NOT NULL, PRIMARY KEY (group_id, permission_rule), FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE);", "创建 group_permissions 表");
    executeAndLog("CREATE TABLE IF NOT EXISTS group_inheritance (group_id INT NOT NULL, parent_group_id INT NOT NULL, PRIMARY KEY (group_id, parent_group_id), FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE, FOREIGN KEY (parent_group_id) REFERENCES permission_groups(id) ON DELETE CASCADE);", "创建 group_inheritance 表");
    executeAndLog("CREATE TABLE IF NOT EXISTS player_groups (player_uuid VARCHAR(36) NOT NULL, group_id INT NOT NULL, PRIMARY KEY (player_uuid, group_id), FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE);", "创建 player_groups 表");

    // ALTER TABLE ADD COLUMN 语法是通用的，但 IF NOT EXISTS 支持不同。
    // 当前实现是直接执行并依赖 db_->execute 处理错误，这对于 ADD COLUMN 是可接受的。
    executeAndLog("ALTER TABLE permission_groups ADD COLUMN priority INT NOT NULL DEFAULT 0;", "向 permission_groups 添加 priority 列 (可能已存在)");

    // --- 添加索引以优化查询 ---
    logger.info("尝试创建索引 (如果不存在)...");
    // db::DatabaseType dbType = db_->getType(); // Already got dbType above

    auto createIndexIfNotExist = [&](const std::string& indexName, const std::string& tableName, const std::string& columnName) {
        std::string sql;
        if (dbType == db::DatabaseType::PostgreSQL || dbType == db::DatabaseType::SQLite) {
            sql = "CREATE INDEX IF NOT EXISTS " + indexName + " ON " + tableName + " (" + columnName + ");";
        } else if (dbType == db::DatabaseType::MySQL) {
            // MySQL does not support IF NOT EXISTS for CREATE INDEX directly.
            // The MySQLDatabase::execute method is expected to handle the duplicate key error (1061) as a warning.
            // We just attempt to create it.
            sql = "CREATE INDEX " + indexName + " ON " + tableName + " (" + columnName + ");";
        } else {
             logger.error("未知数据库类型，无法创建索引。");
             return false;
        }
        return executeAndLog(sql, "为 " + tableName + "." + columnName + " 创建索引 " + indexName);
    };

    createIndexIfNotExist("idx_permissions_name", "permissions", "name");
    createIndexIfNotExist("idx_permission_groups_name", "permission_groups", "name");
    createIndexIfNotExist("idx_player_groups_uuid", "player_groups", "player_uuid");

    logger.info("完成创建索引尝试。");
    // --- 结束添加索引 ---


    logger.info("完成确保权限表的操作。");
}

// 辅助函数：通过名称获取单个 ID，如果未找到或出错则返回空字符串
std::string PermissionManager::getIdByName(const std::string& table, const std::string& name) {
    if (!db_) {
        ll::mod::NativeMod::current()->getLogger().error("在 getIdByName 中数据库未初始化，表 '%s'，名称 '%s'", table.c_str(), name.c_str());
        return "";
    }
std::string sql = "SELECT id FROM " + table + " WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {name});
    if (rows.empty() || rows[0].empty()) {
        return "";
    }
    return rows[0][0];
}

// --- 缓存实现 ---

void PermissionManager::populateGroupCache() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    try {
        logger.info("正在填充权限组缓存...");
        std::string sql = "SELECT id, name FROM permission_groups;";
        auto rows = db_->queryPrepared(sql, {}); // 获取所有组的 ID 和名称

        std::unique_lock lock(cacheMutex_); // 获取独占锁以写入缓存
        groupNameCache_.clear(); // 清除旧缓存
        groupNameCache_.reserve(rows.size()); // 预分配空间

        for (const auto& row : rows) {
            if (row.size() >= 2 && !row[0].empty() && !row[1].empty()) {
                groupNameCache_[row[1]] = row[0]; // 缓存 name -> id
            }
        }
        logger.info("权限组缓存已填充，共 %zu 个条目。", groupNameCache_.size());
    } catch (const std::exception& e) {
        logger.error("填充权限组缓存时异常: %s", e.what());
        // 关键：如果sqlite文件损坏/路径错误/被锁，直接跳过，避免阻塞
        std::unique_lock lock(cacheMutex_);
        groupNameCache_.clear();
    }
}

std::string PermissionManager::getCachedGroupId(const std::string& groupName) {
    // 1. 尝试使用共享锁读取缓存
    {
        std::shared_lock lock(cacheMutex_);
        auto it = groupNameCache_.find(groupName);
        if (it != groupNameCache_.end()) {
            return it->second; // 缓存命中
        }
    } // 共享锁在此处释放

    // 2. 缓存未命中，需要查询数据库并可能写入缓存
    // 在查询数据库之前获取独占锁，以防止其他线程同时查询和写入相同的条目
    std::unique_lock lock(cacheMutex_);

    // 3. 再次检查缓存（Double-Checked Locking 模式）
    // 因为在等待独占锁期间，可能有另一个线程已经填充了缓存
    auto it = groupNameCache_.find(groupName);
    if (it != groupNameCache_.end()) {
        return it->second; // 另一个线程刚刚填充了缓存
    }

    // 4. 缓存中确实没有，查询数据库
    std::string groupId = getIdByName("permission_groups", groupName); // 使用原始 DB 查询

    // 5. 如果在数据库中找到，则更新缓存
    if (!groupId.empty()) {
        groupNameCache_[groupName] = groupId;
    }
    // else {
        // 可选：如果未找到，也可以缓存一个特殊值（例如空字符串或特定标记）
        // 以避免对不存在的组重复查询数据库。
        // groupNameCache_[groupName] = ""; // 缓存未找到状态
    // }

    return groupId; // 返回从数据库找到的 ID 或空字符串
}

void PermissionManager::updateGroupCache(const std::string& groupName, const std::string& groupId) {
    std::unique_lock lock(cacheMutex_);
    groupNameCache_[groupName] = groupId;
}

void PermissionManager::invalidateGroupCache(const std::string& groupName) {
    std::unique_lock lock(cacheMutex_);
    groupNameCache_.erase(groupName);
}

// --- 结束缓存实现 ---


bool PermissionManager::registerPermission(const std::string& name, const std::string& description, bool defaultValue) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("注册权限 '%s'，默认值 %s", name.c_str(), defaultValue ? "true" : "false");
    std::string defaultValueStr = defaultValue ? "1" : "0";

    // 先尝试插入
std::string insertSql = "INSERT INTO permissions (name, description, default_value) VALUES (?, ?, ?) ON CONFLICT (name) DO NOTHING;";
    bool insertOk = db_->executePrepared(insertSql, {name, description, defaultValueStr});

    // 然后更新 (以防它已经存在)
std::string updateSql = "UPDATE permissions SET description = ?, default_value = ? WHERE name = ?;";
    bool updateOk = db_->executePrepared(updateSql, {description, defaultValueStr, name});

    // 如果任一操作逻辑上成功（插入新的或更新现有的），则认为成功
    // 更健壮的检查可能涉及检查受影响的行数（如果数据库 API 支持）。
    // 目前，如果 execute 调用没有返回 false（表示数据库错误），我们假设成功。
    return insertOk && updateOk; // 或者如果 insert 是 IGNORE，则可能仅 updateOk
}

bool PermissionManager::permissionExists(const std::string& name) {
std::string sql = "SELECT 1 FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {name});
    return !rows.empty();
}

std::vector<std::string> PermissionManager::getAllPermissions() {
    std::vector<std::string> list;
    std::string sql = "SELECT name FROM permissions;";
    auto rows = db_->queryPrepared(sql, {}); // 空参数
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}

bool PermissionManager::createGroup(const std::string& groupName, const std::string& description) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    if (!db_) {
        logger.error("尝试创建组 '%s' 时数据库未初始化", groupName.c_str());
        return false;
    }
    logger.info("尝试创建组 '%s'...", groupName.c_str());

    db::DatabaseType dbType = db_->getType();
    std::string insertSql;

    if (dbType == db::DatabaseType::SQLite || dbType == db::DatabaseType::PostgreSQL) {
        // SQLite and PostgreSQL support ON CONFLICT DO NOTHING
        insertSql = "INSERT INTO permission_groups (name, description) VALUES (?, ?) ON CONFLICT (name) DO NOTHING;";
    } else if (dbType == db::DatabaseType::MySQL) {
        // MySQL uses INSERT IGNORE
        insertSql = "INSERT IGNORE INTO permission_groups (name, description) VALUES (?, ?);";
    } else {
        logger.error("未知数据库类型，无法创建组。");
        return false;
    }

    // Execute the insert statement. We don't need the returned ID here,
    // we will query for it by name afterwards.
    bool insertAttemptSuccess = db_->executePrepared(insertSql, {groupName, description});

    // Even if insertAttemptSuccess is false (e.g., a real DB error, not just conflict/ignore),
    // or if it was ignored, we now query by name to get the ID.
    // This handles both cases: group was newly created, or group already existed.
    std::string gid = getIdByName("permission_groups", groupName);

    if (!gid.empty()) {
        // Successfully got the ID, either from a new insert or an existing group
        logger.info("组 '%s' 已存在或已创建 (ID: %s)。", groupName.c_str(), gid.c_str());
        updateGroupCache(groupName, gid); // Update cache
        logger.info("组 '%s' (ID: %s) 缓存已更新。", groupName.c_str(), gid.c_str());
        return true; // Indicate success
    } else {
        // If we still can't get the ID by name, something went wrong during insert
        // or the group simply doesn't exist after the operation.
        logger.error("创建组 '%s' 或获取其 ID 失败。", groupName.c_str());
        // logger.error("数据库错误详情: %s", db_->getLastError().c_str()); // If available
        return false; // Indicate failure
    }
}

bool PermissionManager::groupExists(const std::string& groupName) {
    std::string sql = "SELECT 1 FROM permission_groups WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {groupName});
    return !rows.empty();
}

std::vector<std::string> PermissionManager::getAllGroups() {
    std::vector<std::string> list;
    try {
        ll::mod::NativeMod::current()->getLogger().info("准备查询所有权限组...");
        std::string sql  = "SELECT name FROM permission_groups;";
        auto        rows = db_->queryPrepared(sql, {}); // 空参数
        for (auto& row : rows)
            if (!row.empty()) list.push_back(row[0]);
        ll::mod::NativeMod::current()->getLogger().info("查询权限组完成，数量: %zu", list.size());
    } catch (const std::exception& e) {
        ll::mod::NativeMod::current()->getLogger().error("查询权限组时异常: %s", e.what());
        // 关键：如果sqlite文件损坏/路径错误/被锁，直接返回空列表，避免阻塞
        return {};
    }
    return list;
}

bool PermissionManager::deleteGroup(const std::string& groupName) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    if (!db_) {
        logger.error("尝试删除组 '%s' 时数据库未初始化", groupName.c_str());
        return false;
    }
    logger.info("尝试删除组 '%s'...", groupName.c_str());

    // 使用缓存获取 ID
    std::string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("DeleteGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false; // 组不存在
    }

    // group_permissions, group_inheritance, player_groups 中的外键上的 ON DELETE CASCADE
    // 应该在删除组时自动处理相关删除。
std::string sql = "DELETE FROM permission_groups WHERE id = ?;";
    bool success = db_->executePrepared(sql, {gid});

    if (success) {
        invalidateGroupCache(groupName); // 如果删除成功，则使缓存失效
        logger.info("删除组 '%s' (ID: %s) 成功，缓存已失效。", groupName.c_str(), gid.c_str());
    } else {
         logger.info("删除组 '%s' (ID: %s) 失败。", groupName.c_str(), gid.c_str());
        // logger.error("数据库错误详情: %s", db_->getLastError().c_str());
    }
    return success;
}


bool PermissionManager::addPermissionToGroup(const std::string& groupName, const std::string& permissionRule) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    std::string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        logger.warn("AddPermissionToGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false;
    }

    // 对规则进行简单验证（不能为空，不能仅为 "-"）
    if (permissionRule.empty() || permissionRule == "-") {
         logger.warn("AddPermissionToGroup: 组 '%s' 的权限规则 '%s' 无效。", permissionRule.c_str(), groupName.c_str());
         return false;
    }


    logger.debug("向组 '%s' (GID: %s) 添加权限规则 '%s'",
                permissionRule.c_str(), groupName.c_str(), gid.c_str());

    // MySQL 使用 INSERT IGNORE, SQLite 使用 INSERT OR IGNORE, PostgreSQL 使用 ON CONFLICT
    // 直接插入组 ID 和权限规则字符串。
std::string sql = "INSERT INTO group_permissions (group_id, permission_rule) VALUES (?, ?) ON CONFLICT (group_id, permission_rule) DO NOTHING;";
    return db_->executePrepared(sql, {gid, permissionRule});
}

bool PermissionManager::removePermissionFromGroup(const std::string& groupName, const std::string& permissionRule) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    std::string gid = getCachedGroupId(groupName); // 使用缓存
     if (gid.empty()) {
        // 尝试移除时，如果组不存在 (来自缓存或数据库)，则不发出警告
        return false;
    }

     // 对规则进行简单验证（不能为空，不能仅为 "-"）
    if (permissionRule.empty() || permissionRule == "-") {
         logger.warn("RemovePermissionFromGroup: 组 '%s' 的权限规则 '%s' 无效。", permissionRule.c_str(), groupName.c_str());
         return false;
    }

    //直接根据规则字符串删除。
    logger.info("从组 '%s' (GID: %s) 移除权限规则 '%s'",
                permissionRule.c_str(), groupName.c_str(), gid.c_str());

std::string sql = "DELETE FROM group_permissions WHERE group_id = ? AND permission_rule = ?;";
    return db_->executePrepared(sql, {gid, permissionRule});
}

std::vector<std::string> PermissionManager::getDirectPermissionsOfGroup(const std::string& groupName) {
    std::vector<std::string> perms;
    std::string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        ll::mod::NativeMod::current()->getLogger().warn("getDirectPermissionsOfGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return perms; // 如果组未找到，返回空列表
    }

    // 直接从 group_permissions 选择权限规则字符串
    std::string sql = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
    auto rows = db_->queryPrepared(sql, {gid});
    for (auto& row : rows) {
        if (!row.empty() && !row[0].empty()) { // 确保行和规则字符串不为空
            perms.push_back(row[0]);
        }
    }
    // 可选：排序以保持一致性
    std::sort(perms.begin(), perms.end());
    return perms;
}

// 注意：getParentGroups 内部的 SQL 查询仍然需要连接 permission_groups 表来获取父组名称，
// 但它不需要在函数开始时单独获取 groupName 的 ID。

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
    std::string gid = getCachedGroupId(groupName); // 使用缓存
    std::string pgid = getCachedGroupId(parentGroupName); // 使用缓存
    if (gid.empty() || pgid.empty()) {
         ll::mod::NativeMod::current()->getLogger().warn("AddGroupInheritance: 组 '%s' 或父组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str(), parentGroupName.c_str());
        return false;
    }
    if (gid == pgid) { // 阻止自我继承
         ll::mod::NativeMod::current()->getLogger().warn("AddGroupInheritance: 不能让组 '%s' 继承自身。", groupName.c_str());
        return false;
    }
    // TODO: 添加循环检测？
    // MySQL 使用 INSERT IGNORE, PostgreSQL 使用 ON CONFLICT
    std::string sql = "INSERT INTO group_inheritance (group_id, parent_group_id) VALUES (?, ?) ON CONFLICT (group_id, parent_group_id) DO NOTHING;";
    return db_->executePrepared(sql, {gid, pgid});
}

bool PermissionManager::removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName) {
    std::string gid = getCachedGroupId(groupName); // 使用缓存
    std::string pgid = getCachedGroupId(parentGroupName); // 使用缓存
    if (gid.empty() || pgid.empty()) {
        return false; // 尝试移除不存在的映射 (来自缓存或数据库) 时不发出警告
    }
    std::string sql = "DELETE FROM group_inheritance WHERE group_id = ? AND parent_group_id = ?;";
    return db_->executePrepared(sql, {gid, pgid});
}

#include <map> // 为解析步骤包含 map

std::vector<std::string> PermissionManager::getPermissionsOfGroup(const std::string& groupName) {
    std::vector<std::string> allRules; // 首先在这里收集所有规则
    std::set<std::string> visited; // 防止继承中的循环
    std::function<void(const std::string&)> dfs =
        [&](const std::string& currentGroupName) {
        if (visited.count(currentGroupName)) return;
        visited.insert(currentGroupName);

        // 获取当前组的直接权限规则
        std::string gid = getCachedGroupId(currentGroupName); // 使用缓存获取组 ID
        if (!gid.empty()) {
            std::string directRulesSql = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
            auto directRows = db_->queryPrepared(directRulesSql, {gid});
            for (auto& row : directRows) {
                if (!row.empty() && !row[0].empty()) { // 确保行和规则字符串不为空
                    // 直接将原始规则字符串添加到列表中
                    allRules.push_back(row[0]);
                }
            }
        } else {
             ll::mod::NativeMod::current()->getLogger().warn("getPermissionsOfGroup (DFS): 获取直接规则时未找到组 '%s'。", currentGroupName.c_str());
        }

        // 递归地从父组获取权限
        auto parentGroups = getParentGroups(currentGroupName); // 现在使用预处理语句
        for (const auto& parentGroup : parentGroups) {
            dfs(parentGroup);
        }
    };

    dfs(groupName);

    // --- 解析步骤 ---
    std::map<std::string, bool> effectiveState; // true = 授予, false = 否定
    auto& logger = ll::mod::NativeMod::current()->getLogger(); // 获取 logger 用于调试消息

    for (const auto& rule : allRules) {
        if (rule.empty()) continue;

        bool isNegatedRule = (rule[0] == '-');
        std::string baseName = isNegatedRule ? rule.substr(1) : rule;

        if (baseName.empty()) continue; // 跳过无效规则 "-"

        if (isNegatedRule) {
            // 否定规则总是将状态设置为 false (否定)
            effectiveState[baseName] = false;
            logger.debug("解析规则 '%s': 将 '%s' 的状态设置为否定。", rule.c_str(), baseName.c_str());
        } else {
            // 肯定规则仅在未被明确否定的情况下将状态设置为 true
            if (!effectiveState.count(baseName) || effectiveState[baseName] == true) {
                effectiveState[baseName] = true;
                logger.debug("解析规则 '%s': 将 '%s' 的状态设置为授予 (因为之前未被否定)。", rule.c_str(), baseName.c_str());
            } else {
                logger.debug("解析规则 '%s': 忽略对 '%s' 的肯定授予，因为它已被否定。", rule.c_str(), baseName.c_str());
            }
        }
    }

    // 从解析后的状态构建最终列表
    std::vector<std::string> finalPerms;
    for (const auto& pair : effectiveState) {
        if (pair.second) { // 授予
            finalPerms.push_back(pair.first);
        } else { // 否定
            finalPerms.push_back("-" + pair.first);
        }
    }

    // 可选：排序以保持一致性
    std::sort(finalPerms.begin(), finalPerms.end());

    return finalPerms; // 返回解析后的列表
}


bool PermissionManager::addPlayerToGroup(const std::string& playerUuid, const std::string& groupName) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("将玩家 '%s' 添加到组 '%s'", playerUuid.c_str(), groupName.c_str());
    std::string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        logger.warn("AddPlayerToGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false;
    }
    // MySQL 使用 INSERT IGNORE, SQLite 使用 INSERT OR IGNORE, PostgreSQL 使用 ON CONFLICT
    std::string sql = "INSERT INTO player_groups (player_uuid, group_id) VALUES (?, ?) ON CONFLICT (player_uuid, group_id) DO NOTHING;";
    return db_->executePrepared(sql, {playerUuid, gid});
}

bool PermissionManager::removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.info("从组 '%s' 移除玩家 '%s'", playerUuid.c_str(), groupName.c_str());
    std::string gid = getCachedGroupId(groupName); // 使用缓存
     if (gid.empty()) {
        // 尝试移除时，如果组不存在 (来自缓存或数据库)，则不发出警告
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

std::vector<std::string> PermissionManager::getPlayerGroupIds(const std::string& playerUuid) {
    std::vector<std::string> ids;
    // 除非调试查询问题，否则此处不需要 logger
    std::string sql = "SELECT group_id FROM player_groups WHERE player_uuid = ?;";
    auto rows = db_->queryPrepared(sql, {playerUuid});
    for (auto& row : rows) {
        if (!row.empty()) {
            // 直接推送字符串 ID
            ids.push_back(row[0]);
        }
    }
    return ids;
}


std::vector<std::string> PermissionManager::getPlayersInGroup(const std::string& groupName) {
    std::vector<std::string> list;
    std::string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        // 组不存在 (来自缓存或数据库)，返回空列表
        return list;
    }
    std::string sql = "SELECT player_uuid FROM player_groups WHERE group_id = ?;";
    auto rows = db_->queryPrepared(sql, {gid});
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}


// 辅助函数：将通配符模式转换为正则表达式字符串
std::string wildcardToRegex(const std::string& pattern) {
    std::string regexPatternStr = "^";
    for (char c : pattern) {
        if (c == '*') {
            regexPatternStr += ".*";
        } else if (std::string(".\\+?^$[](){}|").find(c) != std::string::npos) {
            regexPatternStr += '\\'; // 转义正则表达式特殊字符
            regexPatternStr += c;
        } else {
            regexPatternStr += c;
        }
    }
    regexPatternStr += "$";
    return regexPatternStr;
}


std::vector<std::string> PermissionManager::getAllPermissionsForPlayer(const std::string& playerUuid) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("为玩家 '%s' 计算所有有效权限节点", playerUuid.c_str());

    std::set<std::string> effectiveNodes; // 存储最终授予的节点
    std::vector<std::string> positiveRules;
    std::vector<std::string> negativeRules;

    // 1. 获取所有已注册的权限节点名称
    std::vector<std::string> allRegisteredNodes = getAllPermissions();
    std::set<std::string> allRegisteredNodesSet(allRegisteredNodes.begin(), allRegisteredNodes.end()); // 用于更快的查找（如果需要）

    // 2. 添加默认权限
    std::string defaultPermsSql = "SELECT name FROM permissions WHERE default_value = 1;";
    auto defaultRows = db_->queryPrepared(defaultPermsSql, {});
    for (const auto& row : defaultRows) {
        if (!row.empty()) {
            effectiveNodes.insert(row[0]);
            logger.debug("玩家 '%s' 初始拥有默认节点: %s", playerUuid.c_str(), row[0].c_str());
        }
    }

    // 3. 获取玩家的组并按优先级排序
    auto groups = getPlayerGroups(playerUuid);
    if (!groups.empty()) {
        struct GroupInfo { std::string name; int priority; };
        std::vector<GroupInfo> playerGroupInfos;
        playerGroupInfos.reserve(groups.size());
        for (const auto& groupName : groups) {
            playerGroupInfos.push_back({groupName, getGroupPriority(groupName)});
        }
        std::sort(playerGroupInfos.begin(), playerGroupInfos.end(),
                  [](const GroupInfo& a, const GroupInfo& b) { return a.priority > b.priority; });

        logger.debug("玩家 '%s' 的组按优先级排序:", playerUuid.c_str());
        for(const auto& gi : playerGroupInfos) { logger.debug("- 组: %s, 优先级: %d", gi.name.c_str(), gi.priority); }

        // 4. 按优先级顺序收集所有组的规则
        for (const auto& groupInfo : playerGroupInfos) {
            // getPermissionsOfGroup 返回此组的已解析规则（包括继承的）
            auto groupRules = getPermissionsOfGroup(groupInfo.name);
            logger.debug("从组 '%s' (优先级 %d) 收集规则: %zu 条规则", groupInfo.name.c_str(), groupInfo.priority, groupRules.size());
            for (const auto& rule : groupRules) {
                 if (rule.empty()) continue;
                 if (rule[0] == '-') {
                     if (rule.length() > 1) { // 避免添加仅 "-"
                         negativeRules.push_back(rule.substr(1));
                         logger.debug("  收集到否定规则: %s", rule.substr(1).c_str());
                     }
                 } else {
                     positiveRules.push_back(rule);
                     logger.debug("  收集到肯定规则: %s", rule.c_str());
                 }
            }
        }
        // 注意：我们首先收集所有规则，然后处理肯定规则，然后处理否定规则。
        // 这确保了更高优先级的否定会正确覆盖较低优先级的授予。
    } else {
        logger.debug("玩家 '%s' 不属于任何组。", playerUuid.c_str());
    }

    // 5. 展开肯定规则并添加到 effectiveNodes
    logger.debug("展开肯定规则...");
    for (const auto& rule : positiveRules) {
        if (rule.find('*') == std::string::npos) {
            // 精确节点 - 如果已注册则添加（或允许未注册？）- 暂时假设必须注册。
             if (allRegisteredNodesSet.count(rule)) {
                effectiveNodes.insert(rule);
                logger.debug("  从肯定规则添加了精确节点: %s", rule.c_str());
             } else {
                 logger.warn("  肯定规则 '%s' 引用了未注册的权限节点。忽略。", rule.c_str());
             }
        } else {
            // 通配符规则 - 对照已注册节点展开
            try {
                std::regex patternRegex(wildcardToRegex(rule));
                int addedCount = 0;
                for (const auto& node : allRegisteredNodes) {
                    if (std::regex_match(node, patternRegex)) {
                        if (effectiveNodes.insert(node).second) { // Insert 返回 pair<iterator, bool>
                           addedCount++;
                        }
                    }
                }
                 logger.debug("  展开了肯定通配符规则 '%s'，添加了 %d 个匹配的已注册节点。", rule.c_str(), addedCount);
            } catch (const std::regex_error& e) {
                logger.error("  来自肯定规则 '%s' 的无效正则表达式: %s。跳过。", rule.c_str(), e.what());
            }
        }
    }

    // 6. 应用否定规则从 effectiveNodes 中移除节点
    logger.debug("应用否定规则...");
    for (const auto& rule : negativeRules) {
         if (rule.find('*') == std::string::npos) {
             // 精确节点
             if (effectiveNodes.erase(rule)) {
                 logger.debug("  从否定规则移除了精确节点: %s", rule.c_str());
             }
         } else {
             // 通配符规则
             try {
                 std::regex patternRegex(wildcardToRegex(rule));
                 int removedCount = 0;
                 for (auto it = effectiveNodes.begin(); it != effectiveNodes.end(); /* 此处不递增 */) {
                     if (std::regex_match(*it, patternRegex)) {
                         logger.debug("  由于否定通配符规则 '%s'，移除节点 '%s'", it->c_str(), rule.c_str());
                         it = effectiveNodes.erase(it); // 删除并获取下一个迭代器
                         removedCount++;
                     } else {
                         ++it; // 仅在未删除时递增
                     }
                 }
                 if (removedCount > 0) {
                    logger.debug("  应用了否定通配符规则 '%s'，移除了 %d 个节点。", rule.c_str(), removedCount);
                 }
             } catch (const std::regex_error& e) {
                 logger.error("  来自否定规则 '%s' 的无效正则表达式: %s。跳过。", rule.c_str(), e.what());
             }
         }
    }

    // 7. 将最终集合转换为排序后的向量
    std::vector<std::string> finalNodes(effectiveNodes.begin(), effectiveNodes.end());
    std::sort(finalNodes.begin(), finalNodes.end());

    logger.debug("为玩家 '%s' 计算的总有效权限节点数: %zu", playerUuid.c_str(), finalNodes.size());
    return finalNodes;
}


bool PermissionManager::setGroupPriority(const std::string& groupName, int priority) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("为组 '%s' 设置优先级 %d", priority, groupName.c_str());
    // 使用预处理语句检查存在性以保持一致性，尽管在 UPDATE 之前并非严格必要
    if (!groupExists(groupName)) {
         logger.warn("SetGroupPriority: 组 '%s' 未找到。", groupName.c_str());
         return false;
    }
    std::string sql = "UPDATE permission_groups SET priority = ? WHERE name = ?;";
    return db_->executePrepared(sql, {std::to_string(priority), groupName});
}

int PermissionManager::getGroupPriority(const std::string& groupName) {
    std::string sql = "SELECT priority FROM permission_groups WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return 0; // 如果未找到或出错，则默认优先级为 0
    try {
        return std::stoi(rows[0][0]);
    } catch (const std::invalid_argument& ia) {
        ll::mod::NativeMod::current()->getLogger().error("组 '%s' 的优先级值无效: %s", groupName.c_str(), rows[0][0].c_str());
    } catch (const std::out_of_range& oor) {
         ll::mod::NativeMod::current()->getLogger().error("组 '%s' 的优先级值超出范围: %s", groupName.c_str(), rows[0][0].c_str());
    }
    return 0; // 解析错误时返回默认值
}

bool PermissionManager::hasPermission(const std::string& playerUuid, const std::string& permissionNode) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    logger.debug("检查玩家 '%s' 的权限 '%s'", permissionNode.c_str(), playerUuid.c_str());

    // getPlayerGroups, getGroupPriority, getPermissionsOfGroup 现在内部使用预处理语句
    auto groups = getPlayerGroups(playerUuid);

    if (!groups.empty()) {
        struct GroupInfo { std::string name; int priority; };
        std::vector<GroupInfo> playerGroupInfos;
        playerGroupInfos.reserve(groups.size());
        for (const auto& groupName : groups) {
            playerGroupInfos.push_back({groupName, getGroupPriority(groupName)});
        }

        // 按优先级（降序）对组进行排序
        std::sort(playerGroupInfos.begin(), playerGroupInfos.end(),
                  [](const GroupInfo& a, const GroupInfo& b) {
                      return a.priority > b.priority;
                  });

        for (const auto& groupInfo : playerGroupInfos) {
            // getPermissionsOfGroup 已处理继承
            auto groupPermissions = getPermissionsOfGroup(groupInfo.name);
            for (const auto& rule : groupPermissions) {
                bool isNegated = false;
                std::string permissionPattern = rule;
                if (!permissionPattern.empty() && permissionPattern[0] == '-') {
                    isNegated = true;
                    permissionPattern = permissionPattern.substr(1);
                }

                // 将通配符模式转换为正则表达式
                std::string regexPatternStr = "^";
                for (char c : permissionPattern) {
                    if (c == '*') {
                        regexPatternStr += ".*";
                    } else if (std::string(".\\+?^$[](){}|").find(c) != std::string::npos) {
                        regexPatternStr += '\\'; // 转义正则表达式特殊字符
                        regexPatternStr += c;
                    } else {
                        regexPatternStr += c;
                    }
                }
                regexPatternStr += "$";

                try {
                    std::regex permissionRegex(regexPatternStr);
                    if (std::regex_match(permissionNode, permissionRegex)) {
                        logger.debug("权限 '%s' 被组 '%s' (优先级 %d) 中的规则 '%s' %s",
                                     permissionNode.c_str(),
                                     groupInfo.name.c_str(),
                                     groupInfo.priority,
                                     rule.c_str(),
                                     isNegated ? "拒绝" : "授予");
                        return !isNegated; // 找到明确规则，立即返回
                    }
                } catch (const std::regex_error& e) {
                     logger.error("从规则 '%s' 生成的无效正则表达式模式: %s", rule.c_str(), e.what());
                     // 跳过此无效规则
                }
            }
        }
        logger.debug("权限 '%s' 在玩家的组中未明确匹配。", permissionNode.c_str());
    } else {
        logger.debug("玩家 '%s' 不属于任何组。", playerUuid.c_str());
    }

    // 如果没有组规则匹配，则检查权限的默认值
    std::string defaultSql = "SELECT default_value FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(defaultSql, {permissionNode});

    if (!rows.empty() && !rows[0].empty()) {
        try {
            bool defaultValue = std::stoi(rows[0][0]) != 0;
            logger.debug("权限 '%s' 使用默认值: %s", permissionNode.c_str(), defaultValue ? "true" : "false");
            return defaultValue;
        } catch (const std::invalid_argument& ia) {
             logger.error("权限 '%s' 的 default_value 无效: %s", permissionNode.c_str(), rows[0][0].c_str());
        } catch (const std::out_of_range& oor) {
             logger.error("权限 '%s' 的 Default_value 超出范围: %s", permissionNode.c_str(), rows[0][0].c_str());
        }
    } else {
         logger.debug("在 permissions 表中未找到权限节点 '%s'。", permissionNode.c_str());
    }

    // 如果权限节点不存在或默认值无效/缺失，则默认拒绝
    logger.debug("权限 '%s' 被拒绝 (未找到或无适用规则/默认值)。", permissionNode.c_str());
    return false;
}

GroupDetails PermissionManager::getGroupDetails(const std::string& groupName) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    std::string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("getGroupDetails: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return GroupDetails(); // 返回无效的 GroupDetails
    }

    std::string sql = "SELECT id, name, description, priority FROM permission_groups WHERE id = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {gid});

    if (!rows.empty() && rows[0].size() >= 4) {
        try {
            int priority = std::stoi(rows[0][3]);
            return GroupDetails(rows[0][0], rows[0][1], rows[0][2], priority);
        } catch (const std::invalid_argument& ia) {
            logger.error("getGroupDetails: 组 '%s' 的优先级值无效: %s", groupName.c_str(), rows[0][3].c_str());
        } catch (const std::out_of_range& oor) {
            logger.error("getGroupDetails: 组 '%s' 的优先级值超出范围: %s", groupName.c_str(), rows[0][3].c_str());
        }
    }
    logger.warn("getGroupDetails: 无法获取组 '%s' 的详细信息。", groupName.c_str());
    return GroupDetails(); // 返回无效的 GroupDetails
}

bool PermissionManager::updateGroupDescription(const std::string& groupName, const std::string& newDescription) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    std::string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("updateGroupDescription: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false;
    }

    std::string sql = "UPDATE permission_groups SET description = ? WHERE id = ?;";
    bool success = db_->executePrepared(sql, {newDescription, gid});
    if (success) {
        logger.info("成功更新组 '%s' (ID: %s) 的描述。", groupName.c_str(), gid.c_str());
    } else {
        logger.error("更新组 '%s' (ID: %s) 的描述失败。", groupName.c_str(), gid.c_str());
    }
    return success;
}

std::string PermissionManager::getGroupDescription(const std::string& groupName) {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    std::string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("getGroupDescription: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return "";
    }

    std::string sql = "SELECT description FROM permission_groups WHERE id = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {gid});

    if (!rows.empty() && !rows[0].empty()) {
        return rows[0][0];
    }
    return "";
}

} // namespace permission
} // namespace BA
