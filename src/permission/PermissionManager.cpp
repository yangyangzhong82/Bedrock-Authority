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
#include <queue> // For BFS in getChildGroupsRecursive

using namespace std; // 解决 std:: 命名空间问题

namespace BA { // Changed from my_mod
namespace permission {

PermissionManager& PermissionManager::getInstance() {
    static PermissionManager instance;
    return instance;
}

void PermissionManager::init(db::IDatabase* db) {
    db_ = db;
    ensureTables();
    populateGroupCache(); // 初始化时填充组名缓存
    populateGroupPermissionsCache(); // 初始化时填充组权限缓存
    ::ll::mod::NativeMod::current()->getLogger().info("权限管理器已初始化并填充了组缓存和组权限缓存");
}

void PermissionManager::ensureTables() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.info("确保权限表存在...");

    auto executeAndLog = [&](const string& sql, const string& description) {
        bool success = db_->execute(sql);
        logger.info("为 '%s' 执行 SQL: %s. 结果: %s", description.c_str(), sql.c_str(), success ? "成功" : "失败");
        // 注意：ALTER TABLE ADD COLUMN 如果列已存在，在某些数据库（如 SQLite）上可能会返回 false 或抛出异常（取决于实现），但这不一定是关键错误。
        // CREATE TABLE IF NOT EXISTS 应该总是返回 true（除非有严重错误）。
        return success;
    };

    db::DatabaseType dbType = db_->getType();
    string createPermissionsTableSql;
    string createPermissionGroupsTableSql;

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

    auto createIndexIfNotExist = [&](const string& indexName, const string& tableName, const string& columnName) {
        string sql;
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
string PermissionManager::getIdByName(const string& table, const string& name) {
    if (!db_) {
        ::ll::mod::NativeMod::current()->getLogger().error("在 getIdByName 中数据库未初始化，表 '%s'，名称 '%s'", table.c_str(), name.c_str());
        return "";
    }
string sql = "SELECT id FROM " + table + " WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {name});
    if (rows.empty() || rows[0].empty()) {
        return "";
    }
    return rows[0][0];
}

// --- 缓存实现 ---

void PermissionManager::populateGroupCache() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    try {
        logger.info("正在填充权限组缓存...");
        string sql = "SELECT id, name FROM permission_groups;";
        auto rows = db_->queryPrepared(sql, {}); // 获取所有组的 ID 和名称

        std::unique_lock<std::shared_mutex> lock(cacheMutex_); // 获取独占锁以写入缓存
        groupNameCache_.clear(); // 清除旧缓存
        groupNameCache_.reserve(rows.size()); // 预分配空间

        for (const auto& row : rows) {
            if (row.size() >= 2 && !row[0].empty() && !row[1].empty()) {
                groupNameCache_[row[1]] = row[0]; // 缓存 name -> id
            }
        }
        logger.info("权限组缓存已填充，共 %zu 个条目。", groupNameCache_.size());
    } catch (const exception& e) {
        logger.error("填充权限组缓存时异常: %s", e.what());
        // 关键：如果sqlite文件损坏/路径错误/被锁，直接跳过，避免阻塞
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        groupNameCache_.clear();
    }
}

string PermissionManager::getCachedGroupId(const string& groupName) {
    // 1. 尝试使用共享锁读取缓存
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        auto it = groupNameCache_.find(groupName);
        if (it != groupNameCache_.end()) {
            return it->second; // 缓存命中
        }
    } // 共享锁在此处释放

    // 2. 缓存未命中，需要查询数据库并可能写入缓存
    // 在查询数据库之前获取独占锁，以防止其他线程同时查询和写入相同的条目
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    // 3. 再次检查缓存（Double-Checked Locking 模式）
    // 因为在等待独占锁期间，可能有另一个线程已经填充了缓存
    auto it = groupNameCache_.find(groupName);
    if (it != groupNameCache_.end()) {
        return it->second; // 另一个线程刚刚填充了缓存
    }

    // 4. 缓存中确实没有，查询数据库
    string groupId = getIdByName("permission_groups", groupName); // 使用原始 DB 查询

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

void PermissionManager::updateGroupCache(const string& groupName, const string& groupId) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    groupNameCache_[groupName] = groupId;
}

void PermissionManager::invalidateGroupCache(const string& groupName) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    groupNameCache_.erase(groupName);
}

void PermissionManager::populateGroupPermissionsCache() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    try {
        logger.info("正在填充组权限缓存...");
        string sql = "SELECT name FROM permission_groups;";
        auto rows = db_->queryPrepared(sql, {}); // 获取所有组的名称

        std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_); // 获取独占锁以写入缓存
        groupPermissionsCache_.clear(); // 清除旧缓存

        for (const auto& row : rows) {
            if (!row.empty() && !row[0].empty()) {
                string groupName = row[0];
                // 临时解锁，调用 getPermissionsOfGroup，它会递归地计算并返回权限
                // 注意：这里需要一个不使用缓存的 getPermissionsOfGroup 版本，或者确保其内部逻辑能处理缓存未命中
                // 为了避免死锁，这里直接重新计算，或者在 getPermissionsOfGroup 内部处理好锁
                // 考虑到 populateGroupPermissionsCache 只在 init 时调用，可以暂时不考虑死锁
                // 但更安全的做法是，getPermissionsOfGroup 内部的缓存逻辑应该独立于此处的填充
                // 这里我们直接调用 getPermissionsOfGroup，它会自行处理其内部缓存
                // 为了避免循环依赖，我们直接在这里重新实现 getPermissionsOfGroup 的核心逻辑，但不写入缓存
                
                vector<string> allRules; // 首先在这里收集所有规则
                std::set<string> visited; // 防止继承中的循环
                std::function<void(const string&)> dfs =
                    [&](const string& currentGroupName) {
                    if (visited.count(currentGroupName)) return;
                    visited.insert(currentGroupName);

                    // 获取当前组的直接权限规则
                    string gid = getCachedGroupId(currentGroupName); // 使用缓存获取组 ID
                    if (!gid.empty()) {
                        string directRulesSql = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
                        auto directRows = db_->queryPrepared(directRulesSql, {gid});
                        for (auto& r : directRows) {
                            if (!r.empty() && !r[0].empty()) {
                                allRules.push_back(r[0]);
                            }
                        }
                    } else {
                         logger.warn("populateGroupPermissionsCache (DFS): 获取直接规则时未找到组 '%s'。", currentGroupName.c_str());
                    }

                    // 递归地从父组获取权限
                    auto parentGroups = getParentGroups(currentGroupName);
                    for (const auto& parentGroup : parentGroups) {
                        dfs(parentGroup);
                    }
                };

                dfs(groupName);

                // --- 解析步骤 ---
                std::map<string, bool> effectiveState; // true = 授予, false = 否定
                for (const auto& rule : allRules) {
                    if (rule.empty()) continue;

                    bool isNegatedRule = (rule[0] == '-');
                    string baseName = isNegatedRule ? rule.substr(1) : rule;

                    if (baseName.empty()) continue;

                    if (isNegatedRule) {
                        effectiveState[baseName] = false;
                    } else {
                        if (!effectiveState.count(baseName) || effectiveState[baseName] == true) {
                            effectiveState[baseName] = true;
                        }
                    }
                }

                vector<string> finalPerms;
                for (const auto& pair : effectiveState) {
                    if (pair.second) {
                        finalPerms.push_back(pair.first);
                    } else {
                        finalPerms.push_back("-" + pair.first);
                    }
                }
                sort(finalPerms.begin(), finalPerms.end());
                groupPermissionsCache_[groupName] = finalPerms; // 存储到组权限缓存
            }
        }
        logger.info("组权限缓存已填充，共 %zu 个条目。", groupPermissionsCache_.size());
    } catch (const exception& e) {
        logger.error("填充组权限缓存时异常: %s", e.what());
        std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
        groupPermissionsCache_.clear();
    }
}

void PermissionManager::invalidateGroupPermissionsCache(const string& groupName) {
    std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
    groupPermissionsCache_.erase(groupName);
    ::ll::mod::NativeMod::current()->getLogger().debug("组 '%s' 的权限缓存已失效。", groupName.c_str());
}

void PermissionManager::invalidateAllGroupPermissionsCache() {
    std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
    groupPermissionsCache_.clear();
    ::ll::mod::NativeMod::current()->getLogger().debug("所有组的权限缓存已失效。");
}

// --- 结束缓存实现 ---


bool PermissionManager::registerPermission(const string& name, const string& description, bool defaultValue) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.info("注册权限 '%s'，默认值 %s", name.c_str(), defaultValue ? "true" : "false");
    string defaultValueStr = defaultValue ? "1" : "0";

    // 先尝试插入
    string insertSql = "INSERT INTO permissions (name, description, default_value) VALUES (?, ?, ?) ON CONFLICT (name) DO NOTHING;";
    bool insertOk = db_->executePrepared(insertSql, {name, description, defaultValueStr});

    // 然后更新 (以防它已经存在)
    string updateSql = "UPDATE permissions SET description = ?, default_value = ? WHERE name = ?;";
    bool updateOk = db_->executePrepared(updateSql, {description, defaultValueStr, name});

    // 如果任一操作逻辑上成功（插入新的或更新现有的），则认为成功
    // 更健壮的检查可能涉及检查受影响的行数（如果数据库 API 支持）。
    // 目前，如果 execute 调用没有返回 false（表示数据库错误），我们假设成功。
    if (insertOk && updateOk) {
        // 权限注册/更新可能影响所有玩家的默认权限，这里保留全量失效
        invalidateAllPlayerPermissionsCache(); 
        // 注册新权限也可能影响所有组的通配符权限，因此需要使所有组权限缓存失效
        invalidateAllGroupPermissionsCache();
        return true;
    }
    return false; // 或者如果 insert 是 IGNORE，则可能仅 updateOk
}

bool PermissionManager::permissionExists(const string& name) {
string sql = "SELECT 1 FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {name});
    return !rows.empty();
}

vector<string> PermissionManager::getAllPermissions() {
    vector<string> list;
    string sql = "SELECT name FROM permissions;";
    auto rows = db_->queryPrepared(sql, {}); // 空参数
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}

bool PermissionManager::createGroup(const string& groupName, const string& description) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    if (!db_) {
        logger.error("尝试创建组 '%s' 时数据库未初始化", groupName.c_str());
        return false;
    }
    logger.info("尝试创建组 '%s'...", groupName.c_str());

    db::DatabaseType dbType = db_->getType();
    string insertSql;

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
    string gid = getIdByName("permission_groups", groupName);

    if (!gid.empty()) {
        // Successfully got the ID, either from a new insert or an existing group
        logger.info("组 '%s' 已存在或已创建 (ID: %s)。", groupName.c_str(), gid.c_str());
        updateGroupCache(groupName, gid); // Update group name cache
        invalidateGroupPermissionsCache(groupName); // Invalidate this group's permission cache
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

bool PermissionManager::groupExists(const string& groupName) {
    string sql = "SELECT 1 FROM permission_groups WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {groupName});
    return !rows.empty();
}

vector<string> PermissionManager::getAllGroups() {
    vector<string> list;
    try {
        ::ll::mod::NativeMod::current()->getLogger().info("准备查询所有权限组...");
        string sql  = "SELECT name FROM permission_groups;";
        auto        rows = db_->queryPrepared(sql, {}); // 空参数
        for (auto& row : rows)
            if (!row.empty()) list.push_back(row[0]);
        ::ll::mod::NativeMod::current()->getLogger().info("查询权限组完成，数量: %zu", list.size());
    } catch (const exception& e) {
        ::ll::mod::NativeMod::current()->getLogger().error("查询权限组时异常: %s", e.what());
        // 关键：如果sqlite文件损坏/路径错误/被锁，直接返回空列表，避免阻塞
        return {};
    }
    return list;
}

bool PermissionManager::deleteGroup(const string& groupName) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    if (!db_) {
        logger.error("尝试删除组 '%s' 时数据库未初始化", groupName.c_str());
        return false;
    }
    logger.info("尝试删除组 '%s'...", groupName.c_str());

    // 使用缓存获取 ID
    string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("DeleteGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false; // 组不存在
    }

    // 获取受影响的玩家
    vector<string> affectedPlayers = getAffectedPlayersByGroup(groupName);
    // 获取所有继承此组的子组，这些子组的权限缓存也需要失效
    std::set<string> childGroups = getChildGroupsRecursive(groupName);

    // group_permissions, group_inheritance, player_groups 中的外键上的 ON DELETE CASCADE
    // 应该在删除组时自动处理相关删除。
    string sql = "DELETE FROM permission_groups WHERE id = ?;";
    bool success = db_->executePrepared(sql, {gid});

    if (success) {
        invalidateGroupCache(groupName); // 如果删除成功，则使组名缓存失效
        invalidateGroupPermissionsCache(groupName); // 使被删除组的权限缓存失效
        // 使所有受影响的子组的权限缓存失效
        for (const string& childGroup : childGroups) {
            invalidateGroupPermissionsCache(childGroup);
        }
        // 对受影响的玩家调用 invalidatePlayerPermissionsCache
        for (const string& playerUuid : affectedPlayers) {
            invalidatePlayerPermissionsCache(playerUuid);
        }
        logger.info("删除组 '%s' (ID: %s) 成功，相关缓存已失效。", groupName.c_str(), gid.c_str());
    } else {
         logger.info("删除组 '%s' (ID: %s) 失败。", groupName.c_str(), gid.c_str());
        // logger.error("数据库错误详情: %s", db_->getLastError().c_str());
    }
    return success;
}


bool PermissionManager::addPermissionToGroup(const string& groupName, const string& permissionRule) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    string gid = getCachedGroupId(groupName); // 使用缓存
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
string sql = "INSERT INTO group_permissions (group_id, permission_rule) VALUES (?, ?) ON CONFLICT (group_id, permission_rule) DO NOTHING;";
    bool success = db_->executePrepared(sql, {gid, permissionRule});
    if (success) {
        // 组权限变化，使该组及其所有子组的权限缓存失效
        std::set<string> affectedGroups = getChildGroupsRecursive(groupName);
        for (const string& affectedGroup : affectedGroups) {
            invalidateGroupPermissionsCache(affectedGroup);
        }
        // 获取受影响的玩家
        vector<string> affectedPlayers = getAffectedPlayersByGroup(groupName);
        for (const string& playerUuid : affectedPlayers) {
            invalidatePlayerPermissionsCache(playerUuid);
        }
    }
    return success;
}

bool PermissionManager::removePermissionFromGroup(const string& groupName, const string& permissionRule) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    string gid = getCachedGroupId(groupName); // 使用缓存
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

string sql = "DELETE FROM group_permissions WHERE group_id = ? AND permission_rule = ?;";
    bool success = db_->executePrepared(sql, {gid, permissionRule});
    if (success) {
        // 组权限变化，使该组及其所有子组的权限缓存失效
        std::set<string> affectedGroups = getChildGroupsRecursive(groupName);
        for (const string& affectedGroup : affectedGroups) {
            invalidateGroupPermissionsCache(affectedGroup);
        }
        // 获取受影响的玩家
        vector<string> affectedPlayers = getAffectedPlayersByGroup(groupName);
        for (const string& playerUuid : affectedPlayers) {
            invalidatePlayerPermissionsCache(playerUuid);
        }
    }
    return success;
}

vector<string> PermissionManager::getDirectPermissionsOfGroup(const string& groupName) {
    vector<string> perms;
    string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        ::ll::mod::NativeMod::current()->getLogger().warn("getDirectPermissionsOfGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return perms; // 如果组未找到，返回空列表
    }

    // 直接从 group_permissions 选择权限规则字符串
    string sql = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
    auto rows = db_->queryPrepared(sql, {gid});
    for (auto& row : rows) {
        if (!row.empty() && !row[0].empty()) { // 确保行和规则字符串不为空
            perms.push_back(row[0]);
        }
    }
    // 可选：排序以保持一致性
    sort(perms.begin(), perms.end());
    return perms;
}

// 注意：getParentGroups 内部的 SQL 查询仍然需要连接 permission_groups 表来获取父组名称，
// 但它不需要在函数开始时单独获取 groupName 的 ID。

vector<string> PermissionManager::getParentGroups(const string& groupName) {
    vector<string> parents;
    string sql = "SELECT pg2.name FROM permission_groups pg1 "
                      "JOIN group_inheritance gi ON pg1.id = gi.group_id "
                      "JOIN permission_groups pg2 ON gi.parent_group_id = pg2.id "
                      "WHERE pg1.name = ?;";
    auto rows = db_->queryPrepared(sql, {groupName});
    for (auto& row : rows) if (!row.empty()) parents.push_back(row[0]);
    return parents;
}

bool PermissionManager::addGroupInheritance(const string& groupName, const string& parentGroupName) {
    string gid = getCachedGroupId(groupName); // 使用缓存
    string pgid = getCachedGroupId(parentGroupName); // 使用缓存
    if (gid.empty() || pgid.empty()) {
         ::ll::mod::NativeMod::current()->getLogger().warn("AddGroupInheritance: 组 '%s' 或父组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str(), parentGroupName.c_str());
        return false;
    }
    if (gid == pgid) { // 阻止自我继承
         ::ll::mod::NativeMod::current()->getLogger().warn("AddGroupInheritance: 不能让组 '%s' 继承自身。", groupName.c_str());
        return false;
    }
    // TODO: 添加循环检测？
    // MySQL 使用 INSERT IGNORE, PostgreSQL 使用 ON CONFLICT
    string sql = "INSERT INTO group_inheritance (group_id, parent_group_id) VALUES (?, ?) ON CONFLICT (group_id, parent_group_id) DO NOTHING;";
    bool success = db_->executePrepared(sql, {gid, pgid});
    if (success) {
        // 继承关系变化，使子组及其所有子组的权限缓存失效
        std::set<string> affectedGroups = getChildGroupsRecursive(groupName);
        for (const string& affectedGroup : affectedGroups) {
            invalidateGroupPermissionsCache(affectedGroup);
        }
        // 获取受影响的玩家
        vector<string> affectedPlayers = getAffectedPlayersByGroup(groupName);
        for (const string& playerUuid : affectedPlayers) {
            invalidatePlayerPermissionsCache(playerUuid);
        }
    }
    return success;
}

bool PermissionManager::removeGroupInheritance(const string& groupName, const string& parentGroupName) {
    string gid = getCachedGroupId(groupName); // 使用缓存
    string pgid = getCachedGroupId(parentGroupName); // 使用缓存
    if (gid.empty() || pgid.empty()) {
        return false; // 尝试移除不存在的映射 (来自缓存或数据库) 时不发出警告
    }
    string sql = "DELETE FROM group_inheritance WHERE group_id = ? AND parent_group_id = ?;";
    bool success = db_->executePrepared(sql, {gid, pgid});
    if (success) {
        // 继承关系变化，使子组及其所有子组的权限缓存失效
        std::set<string> affectedGroups = getChildGroupsRecursive(groupName);
        for (const string& affectedGroup : affectedGroups) {
            invalidateGroupPermissionsCache(affectedGroup);
        }
        // 获取受影响的玩家
        vector<string> affectedPlayers = getAffectedPlayersByGroup(groupName);
        for (const string& playerUuid : affectedPlayers) {
            invalidatePlayerPermissionsCache(playerUuid);
        }
    }
    return success;
}

// #include <map> // 为解析步骤包含 map (已在文件顶部包含)

vector<string> PermissionManager::getPermissionsOfGroup(const string& groupName) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("为组 '%s' 获取最终权限节点", groupName.c_str());

    // 1. 尝试从缓存读取
    {
        std::shared_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
        auto it = groupPermissionsCache_.find(groupName);
        if (it != groupPermissionsCache_.end()) {
            logger.debug("组 '%s' 的权限从缓存中获取。", groupName.c_str());
            return it->second; // 缓存命中
        }
    } // 共享锁在此处释放

    // 2. 缓存未命中，需要计算并写入缓存
    vector<string> allRules; // 首先在这里收集所有规则
    std::set<string> visited; // 防止继承中的循环
    std::function<void(const string&)> dfs =
        [&](const string& currentGroupName) {
        if (visited.count(currentGroupName)) return;
        visited.insert(currentGroupName);

        // 获取当前组的直接权限规则
        string gid = getCachedGroupId(currentGroupName); // 使用缓存获取组 ID
        if (!gid.empty()) {
            string directRulesSql = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
            auto directRows = db_->queryPrepared(directRulesSql, {gid});
            for (auto& row : directRows) {
                if (!row.empty() && !row[0].empty()) { // 确保行和规则字符串不为空
                    // 直接将原始规则字符串添加到列表中
                    allRules.push_back(row[0]);
                }
            }
        } else {
             logger.warn("getPermissionsOfGroup (DFS): 获取直接规则时未找到组 '%s'。", currentGroupName.c_str());
        }

        // 递归地从父组获取权限
        auto parentGroups = getParentGroups(currentGroupName); // 现在使用预处理语句
        for (const auto& parentGroup : parentGroups) {
            dfs(parentGroup);
        }
    };

    dfs(groupName);

    // --- 解析步骤 ---
    std::map<string, bool> effectiveState; // true = 授予, false = 否定
    // auto& logger = ::ll::mod::NativeMod::current()->getLogger(); // 获取 logger 用于调试消息 (已在函数开头获取)

    for (const auto& rule : allRules) {
        if (rule.empty()) continue;

        bool isNegatedRule = (rule[0] == '-');
        string baseName = isNegatedRule ? rule.substr(1) : rule;

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
    vector<string> finalPerms;
    for (const auto& pair : effectiveState) {
        if (pair.second) { // 授予
            finalPerms.push_back(pair.first);
        } else { // 否定
            finalPerms.push_back("-" + pair.first);
        }
    }

    // 可选：排序以保持一致性
    sort(finalPerms.begin(), finalPerms.end());

    // 3. 将结果存储到缓存
    {
        std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
        groupPermissionsCache_[groupName] = finalPerms;
        logger.debug("组 '%s' 的权限已缓存。", groupName.c_str());
    }

    return finalPerms; // 返回解析后的列表
}


bool PermissionManager::addPlayerToGroup(const string& playerUuid, const string& groupName) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.info("将玩家 '%s' 添加到组 '%s'", playerUuid.c_str(), groupName.c_str());
    string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        logger.warn("AddPlayerToGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false;
    }
    // MySQL 使用 INSERT IGNORE, SQLite 使用 INSERT OR IGNORE, PostgreSQL 使用 ON CONFLICT
    string sql = "INSERT INTO player_groups (player_uuid, group_id) VALUES (?, ?) ON CONFLICT (player_uuid, group_id) DO NOTHING;";
    bool success = db_->executePrepared(sql, {playerUuid, gid});
    if (success) {
        invalidatePlayerPermissionsCache(playerUuid); // 玩家组变化，使该玩家缓存失效
    }
    return success;
}

bool PermissionManager::removePlayerFromGroup(const string& playerUuid, const string& groupName) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.info("从组 '%s' 移除玩家 '%s'", playerUuid.c_str(), groupName.c_str());
    string gid = getCachedGroupId(groupName); // 使用缓存
     if (gid.empty()) {
        // 尝试移除时，如果组不存在 (来自缓存或数据库)，则不发出警告
        return false;
    }
    string sql = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";
    bool success = db_->executePrepared(sql, {playerUuid, gid});
    if (success) {
        invalidatePlayerPermissionsCache(playerUuid); // 玩家组变化，使该玩家缓存失效
    }
    return success;
}

vector<string> PermissionManager::getPlayerGroups(const string& playerUuid) {
    vector<string> list;
    string sql = "SELECT pg.name FROM permission_groups pg "
                      "JOIN player_groups pgp ON pg.id = pgp.group_id "
                      "WHERE pgp.player_uuid = ?;";
    auto rows = db_->queryPrepared(sql, {playerUuid});
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}

vector<string> PermissionManager::getPlayerGroupIds(const string& playerUuid) {
    vector<string> ids;
    // 除非调试查询问题，否则此处不需要 logger
    string sql = "SELECT group_id FROM player_groups WHERE player_uuid = ?;";
    auto rows = db_->queryPrepared(sql, {playerUuid});
    for (auto& row : rows) {
        if (!row.empty()) {
            // 直接推送字符串 ID
            ids.push_back(row[0]);
        }
    }
    return ids;
}


vector<string> PermissionManager::getPlayersInGroup(const string& groupName) {
    vector<string> list;
    string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        // 组不存在 (来自缓存或数据库)，返回空列表
        return list;
    }
    string sql = "SELECT player_uuid FROM player_groups WHERE group_id = ?;";
    auto rows = db_->queryPrepared(sql, {gid});
    for (auto& row : rows) if (!row.empty()) list.push_back(row[0]);
    return list;
}


// 辅助函数：将通配符模式转换为正则表达式字符串
string PermissionManager::wildcardToRegex(const string& pattern) {
    string regexPatternStr = "^";
    for (char c : pattern) {
        if (c == '*') {
            regexPatternStr += ".*";
        } else if (string(".\\+?^$[](){}|").find(c) != string::npos) {
            regexPatternStr += '\\'; // 转义正则表达式特殊字符
            regexPatternStr += c;
        } else {
            regexPatternStr += c;
        }
    }
    regexPatternStr += "$";
    return regexPatternStr;
}

// 使特定玩家的权限缓存失效
void PermissionManager::invalidatePlayerPermissionsCache(const string& playerUuid) {
    std::unique_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
    playerPermissionsCache_.erase(playerUuid);
    ::ll::mod::NativeMod::current()->getLogger().debug("玩家 '%s' 的权限缓存已失效。", playerUuid.c_str());
}

// 使所有玩家的权限缓存失效
void PermissionManager::invalidateAllPlayerPermissionsCache() {
    std::unique_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
    playerPermissionsCache_.clear();
    ::ll::mod::NativeMod::current()->getLogger().debug("所有玩家的权限缓存已失效。");
}

// 新增辅助函数：递归获取所有子组
std::set<string> PermissionManager::getChildGroupsRecursive(const string& groupName) {
    std::set<string> allChildGroups;
    std::queue<string> q;
    q.push(groupName);
    allChildGroups.insert(groupName); // Include the starting group itself

    while (!q.empty()) {
        string currentGroup = q.front();
        q.pop();

        // Find groups that inherit from currentGroup (i.e., currentGroup is a parent)
        string sql = "SELECT pg1.name FROM permission_groups pg1 "
                     "JOIN group_inheritance gi ON pg1.id = gi.group_id "
                     "JOIN permission_groups pg2 ON gi.parent_group_id = pg2.id "
                     "WHERE pg2.name = ?;";
        auto rows = db_->queryPrepared(sql, {currentGroup});

        for (const auto& row : rows) {
            if (!row.empty() && !row[0].empty()) {
                string childGroup = row[0];
                if (allChildGroups.find(childGroup) == allChildGroups.end()) {
                    allChildGroups.insert(childGroup);
                    q.push(childGroup);
                }
            }
        }
    }
    return allChildGroups;
}

// 新增辅助函数：获取受特定组修改影响的所有玩家 UUID
vector<string> PermissionManager::getAffectedPlayersByGroup(const string& groupName) {
    std::set<string> affectedPlayersSet;
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();

    // 1. 找出所有属于这个组的玩家
    vector<string> directPlayers = getPlayersInGroup(groupName);
    for (const string& uuid : directPlayers) {
        affectedPlayersSet.insert(uuid);
    }
    logger.debug("组 '%s' 的直接玩家数: %zu", groupName.c_str(), directPlayers.size());

    // 2. 找出所有继承了这个组的组，并递归地找出所有属于这些子组的玩家
    std::set<string> allRelatedGroups = getChildGroupsRecursive(groupName);
    logger.debug("组 '%s' 及其所有子组（包括自身）总数: %zu", groupName.c_str(), allRelatedGroups.size());

    for (const string& relatedGroup : allRelatedGroups) {
        vector<string> playersInRelatedGroup = getPlayersInGroup(relatedGroup);
        for (const string& uuid : playersInRelatedGroup) {
            affectedPlayersSet.insert(uuid);
        }
    }

    return vector<string>(affectedPlayersSet.begin(), affectedPlayersSet.end());
}


vector<string> PermissionManager::getAllPermissionsForPlayer(const string& playerUuid) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("为玩家 '%s' 计算所有有效权限节点", playerUuid.c_str());

    // 1. 尝试从缓存读取
    {
        std::shared_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
        auto it = playerPermissionsCache_.find(playerUuid);
        if (it != playerPermissionsCache_.end()) {
            logger.debug("玩家 '%s' 的权限从缓存中获取。", playerUuid.c_str());
            return it->second; // 缓存命中
        }
    } // 共享锁在此处释放

    // 2. 缓存未命中，需要计算并写入缓存
    std::set<string> effectiveNodes; // 存储最终授予的节点
    vector<string> positiveRules;
    vector<string> negativeRules;

    // 1. 获取所有已注册的权限节点名称
    vector<string> allRegisteredNodes = getAllPermissions();
    std::set<string> allRegisteredNodesSet(allRegisteredNodes.begin(), allRegisteredNodes.end()); // 用于更快的查找（如果需要）

    // 2. 添加默认权限
    string defaultPermsSql = "SELECT name FROM permissions WHERE default_value = 1;";
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
        struct GroupInfo { string name; int priority; };
        vector<GroupInfo> playerGroupInfos;
        playerGroupInfos.reserve(groups.size());
        for (const auto& groupName : groups) {
            playerGroupInfos.push_back({groupName, getGroupPriority(groupName)});
        }
        sort(playerGroupInfos.begin(), playerGroupInfos.end(),
                  [](const GroupInfo& a, const GroupInfo& b) { return a.priority > b.priority; });

        logger.debug("玩家 '%s' 的组按优先级排序:", playerUuid.c_str());
        for(const auto& gi : playerGroupInfos) { logger.debug("- 组: %s, 优先级: %d", gi.name.c_str(), gi.priority); }

        // 4. 按优先级顺序收集所有组的规则
        for (const auto& groupInfo : playerGroupInfos) {
            // getPermissionsOfGroup 返回此组的已解析规则（包括继承的），并利用了组权限缓存
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
        if (rule.find('*') == string::npos) {
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
         if (rule.find('*') == string::npos) {
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
    vector<string> finalNodes(effectiveNodes.begin(), effectiveNodes.end());
    sort(finalNodes.begin(), finalNodes.end());

    // 8. 将结果存储到缓存
    {
        std::unique_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
        playerPermissionsCache_[playerUuid] = finalNodes;
        logger.debug("玩家 '%s' 的权限已缓存。", playerUuid.c_str());
    }

    logger.debug("为玩家 '%s' 计算的总有效权限节点数: %zu", playerUuid.c_str(), finalNodes.size());
    return finalNodes;
}


bool PermissionManager::setGroupPriority(const string& groupName, int priority) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("为组 '%s' 设置优先级 %d", groupName.c_str(), priority); // 修正参数顺序
    // 使用预处理语句检查存在性以保持一致性，尽管在 UPDATE 之前并非严格必要
    if (!groupExists(groupName)) {
         logger.warn("SetGroupPriority: 组 '%s' 未找到。", groupName.c_str());
         return false;
    }
    string sql = "UPDATE permission_groups SET priority = ? WHERE name = ?;";
    bool success = db_->executePrepared(sql, {to_string(priority), groupName});
    if (success) {
        // 优先级变化，使该组及其所有子组的权限缓存失效
        std::set<string> affectedGroups = getChildGroupsRecursive(groupName);
        for (const string& affectedGroup : affectedGroups) {
            invalidateGroupPermissionsCache(affectedGroup);
        }
        // 获取受影响的玩家
        vector<string> affectedPlayers = getAffectedPlayersByGroup(groupName);
        for (const string& playerUuid : affectedPlayers) {
            invalidatePlayerPermissionsCache(playerUuid);
        }
    }
    return success;
}

int PermissionManager::getGroupPriority(const string& groupName) {
    string sql = "SELECT priority FROM permission_groups WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) return 0; // 如果未找到或出错，则默认优先级为 0
    try {
        return stoi(rows[0][0]);
    } catch (const invalid_argument& ia) {
        ::ll::mod::NativeMod::current()->getLogger().error("组 '%s' 的优先级值无效: %s", groupName.c_str(), rows[0][0].c_str());
    } catch (const out_of_range& oor) {
         ::ll::mod::NativeMod::current()->getLogger().error("组 '%s' 的优先级值超出范围: %s", groupName.c_str(), rows[0][0].c_str());
    }
    return 0; // 解析错误时返回默认值
}

bool PermissionManager::hasPermission(const string& playerUuid, const string& permissionNode) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("检查玩家 '%s' 的权限 '%s'", playerUuid.c_str(), permissionNode.c_str());

    // 获取玩家的所有有效权限（这将利用缓存）
    vector<string> playerEffectivePermissions = getAllPermissionsForPlayer(playerUuid);

    // 遍历玩家的有效权限，检查是否有匹配的规则
    for (const auto& rule : playerEffectivePermissions) {
        bool isNegated = false;
        string permissionPattern = rule;
        if (!permissionPattern.empty() && permissionPattern[0] == '-') {
            isNegated = true;
            permissionPattern = permissionPattern.substr(1);
        }

        // 将通配符模式转换为正则表达式
        // 注意：这里直接使用 wildcardToRegex 辅助函数，它现在是成员函数
        string regexPatternStr = wildcardToRegex(permissionPattern);

        try {
            std::regex permissionRegex(regexPatternStr);
            if (std::regex_match(permissionNode, permissionRegex)) {
                logger.debug("权限 '%s' 被玩家 '%s' 的规则 '%s' %s",
                             permissionNode.c_str(),
                             playerUuid.c_str(),
                             rule.c_str(),
                             isNegated ? "拒绝" : "授予");
                return !isNegated; // 找到明确规则，立即返回
            }
        } catch (const std::regex_error& e) {
             logger.error("从规则 '%s' 生成的无效正则表达式模式: %s", rule.c_str(), e.what());
             // 跳过此无效规则
        }
    }

    logger.debug("权限 '%s' 在玩家 '%s' 的有效权限中未明确匹配。", permissionNode.c_str(), playerUuid.c_str());

    // 如果没有有效权限规则匹配，则检查权限的默认值
    string defaultSql = "SELECT default_value FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(defaultSql, {permissionNode});

    if (!rows.empty() && !rows[0].empty()) {
        try {
            bool defaultValue = stoi(rows[0][0]) != 0;
            logger.debug("权限 '%s' 使用默认值: %s", permissionNode.c_str(), defaultValue ? "true" : "false");
            return defaultValue;
        } catch (const invalid_argument& ia) {
             ::ll::mod::NativeMod::current()->getLogger().error("权限 '%s' 的 default_value 无效: %s", permissionNode.c_str(), rows[0][0].c_str());
        } catch (const out_of_range& oor) {
             ::ll::mod::NativeMod::current()->getLogger().error("权限 '%s' 的 Default_value 超出范围: %s", permissionNode.c_str(), rows[0][0].c_str());
        }
    } else {
         logger.debug("在 permissions 表中未找到权限节点 '%s'。", permissionNode.c_str());
    }

    // 如果权限节点不存在或默认值无效/缺失，则默认拒绝
    logger.debug("权限 '%s' 被拒绝 (未找到或无适用规则/默认值)。", permissionNode.c_str());
    return false;
}

GroupDetails PermissionManager::getGroupDetails(const string& groupName) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("getGroupDetails: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return GroupDetails(); // 返回无效的 GroupDetails
    }

    string sql = "SELECT id, name, description, priority FROM permission_groups WHERE id = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {gid});

    if (!rows.empty() && rows[0].size() >= 4) {
        try {
            int priority = stoi(rows[0][3]);
            return GroupDetails(rows[0][0], rows[0][1], rows[0][2], priority);
        } catch (const invalid_argument& ia) {
            logger.error("getGroupDetails: 组 '%s' 的优先级值无效: %s", groupName.c_str(), rows[0][3].c_str());
        } catch (const out_of_range& oor) {
            logger.error("getGroupDetails: 组 '%s' 的优先级值超出范围: %s", groupName.c_str(), rows[0][3].c_str());
        }
    }
    logger.warn("getGroupDetails: 无法获取组 '%s' 的详细信息。", groupName.c_str());
    return GroupDetails(); // 返回无效的 GroupDetails
}

bool PermissionManager::updateGroupDescription(const string& groupName, const string& newDescription) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("updateGroupDescription: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false;
    }

    string sql = "UPDATE permission_groups SET description = ? WHERE id = ?;";
    bool success = db_->executePrepared(sql, {newDescription, gid});
    if (success) {
        logger.info("成功更新组 '%s' (ID: %s) 的描述。", groupName.c_str(), gid.c_str());
    } else {
        logger.error("更新组 '%s' (ID: %s) 的描述失败。", groupName.c_str(), gid.c_str());
    }
    return success;
}

string PermissionManager::getGroupDescription(const string& groupName) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    string gid = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("getGroupDescription: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return "";
    }

    string sql = "SELECT description FROM permission_groups WHERE id = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {gid});

    if (!rows.empty() && !rows[0].empty()) {
        return rows[0][0];
    }
    return "";
}

} // namespace permission
} // namespace BA
