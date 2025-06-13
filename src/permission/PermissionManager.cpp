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
#include <chrono> // For sleep in worker thread

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

    // 启动异步任务工作线程
    running_ = true;
    workerThread_ = std::thread(&PermissionManager::processTasks, this);
    ::ll::mod::NativeMod::current()->getLogger().info("权限管理器异步缓存失效工作线程已启动。");
}

void PermissionManager::shutdown() {
    if (running_) {
        // 发送停止信号
        enqueueTask({CacheInvalidationTaskType::SHUTDOWN, ""});
        // 等待工作线程完成
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
        running_ = false;
        ::ll::mod::NativeMod::current()->getLogger().info("权限管理器异步缓存失效工作线程已停止。");
    }
}

// 将任务推入队列
void PermissionManager::enqueueTask(CacheInvalidationTask task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        taskQueue_.push(std::move(task));
    }
    condition_.notify_one(); // 通知工作线程有新任务
}

// 工作线程处理任务的函数
void PermissionManager::processTasks() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    while (true) {
        CacheInvalidationTask task;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            condition_.wait(lock, [this] { return !taskQueue_.empty() || !running_; });

            if (!running_ && taskQueue_.empty()) {
                logger.debug("异步任务队列工作线程收到停止信号并队列为空，正在退出。");
                break; // 退出循环
            }

            task = std::move(taskQueue_.front());
            taskQueue_.pop();
        } // 锁在此处释放

        try {
            switch (task.type) {
                case CacheInvalidationTaskType::GROUP_MODIFIED: {
                    logger.debug("处理 GROUP_MODIFIED 任务，组名: %s", task.data.c_str());
                    // 获取所有继承此组的子组，这些子组的权限缓存也需要失效
                    std::set<string> affectedGroups = getChildGroupsRecursive(task.data);
                    for (const string& affectedGroup : affectedGroups) {
                        _invalidateGroupPermissionsCache(affectedGroup);
                    }
                    // 获取受影响的玩家
                    vector<string> affectedPlayers = getAffectedPlayersByGroup(task.data);
                    for (const string& playerUuid : affectedPlayers) {
                        _invalidatePlayerPermissionsCache(playerUuid);
                    }
                    break;
                }
                case CacheInvalidationTaskType::PLAYER_GROUP_CHANGED: {
                    logger.debug("处理 PLAYER_GROUP_CHANGED 任务，玩家UUID: %s", task.data.c_str());
                    _invalidatePlayerPermissionsCache(task.data);
                    break;
                }
                case CacheInvalidationTaskType::ALL_GROUPS_MODIFIED: {
                    logger.debug("处理 ALL_GROUPS_MODIFIED 任务。");
                    _invalidateAllGroupPermissionsCache();
                    break;
                }
                case CacheInvalidationTaskType::ALL_PLAYERS_MODIFIED: {
                    logger.debug("处理 ALL_PLAYERS_MODIFIED 任务。");
                    _invalidateAllPlayerPermissionsCache();
                    break;
                }
                case CacheInvalidationTaskType::SHUTDOWN: {
                    logger.debug("异步任务队列工作线程收到 SHUTDOWN 任务，正在退出。");
                    return; // 退出线程
                }
            }
        } catch (const std::exception& e) {
            logger.error("异步缓存失效任务处理异常: %s", e.what());
        } catch (...) {
            logger.error("异步缓存失效任务处理未知异常。");
        }
    }
}

// 实际执行缓存失效的内部函数
void PermissionManager::_invalidateGroupPermissionsCache(const string& groupName) {
    std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
    groupPermissionsCache_.erase(groupName);
    ::ll::mod::NativeMod::current()->getLogger().debug("组 '%s' 的权限缓存已失效 (由工作线程)。", groupName.c_str());
}

void PermissionManager::_invalidatePlayerPermissionsCache(const string& playerUuid) {
    std::unique_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
    playerPermissionsCache_.erase(playerUuid);
    ::ll::mod::NativeMod::current()->getLogger().debug("玩家 '%s' 的权限缓存已失效 (由工作线程)。", playerUuid.c_str());
}

void PermissionManager::_invalidateAllGroupPermissionsCache() {
    std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
    groupPermissionsCache_.clear();
    ::ll::mod::NativeMod::current()->getLogger().debug("所有组的权限缓存已失效 (由工作线程)。");
}

void PermissionManager::_invalidateAllPlayerPermissionsCache() {
    std::unique_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
    playerPermissionsCache_.clear();
    ::ll::mod::NativeMod::current()->getLogger().debug("所有玩家的权限缓存已失效 (由工作线程)。");
}

void PermissionManager::ensureTables() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("确保权限表存在...");

    auto executeAndLog = [&](const string& sql, const string& description) {
        bool success = db_->execute(sql);
        logger.debug("为 '%s' 执行 SQL: %s. 结果: %s", description.c_str(), sql.c_str(), success ? "成功" : "失败");
        return success;
    };

    // 使用数据库方言方法创建表
    executeAndLog(
        db_->getCreateTableSql(
            "permissions",
            "id " + db_->getAutoIncrementPrimaryKeyDefinition() + ", "
            "name VARCHAR(255) UNIQUE NOT NULL, "
            "description TEXT, "
            "default_value INT NOT NULL DEFAULT 0"
        ),
        "创建 permissions 表"
    );
    executeAndLog(
        db_->getCreateTableSql(
            "permission_groups",
            "id " + db_->getAutoIncrementPrimaryKeyDefinition() + ", "
            "name VARCHAR(255) UNIQUE NOT NULL, "
            "description TEXT"
        ),
        "创建 permission_groups 表"
    );

    // group_permissions, group_inheritance, player_groups 表的语法是通用的
    executeAndLog("CREATE TABLE IF NOT EXISTS group_permissions (group_id INT NOT NULL, permission_rule VARCHAR(255) NOT NULL, PRIMARY KEY (group_id, permission_rule), FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE);", "创建 group_permissions 表");
    executeAndLog("CREATE TABLE IF NOT EXISTS group_inheritance (group_id INT NOT NULL, parent_group_id INT NOT NULL, PRIMARY KEY (group_id, parent_group_id), FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE, FOREIGN KEY (parent_group_id) REFERENCES permission_groups(id) ON DELETE CASCADE);", "创建 group_inheritance 表");
    executeAndLog("CREATE TABLE IF NOT EXISTS player_groups (player_uuid VARCHAR(36) NOT NULL, group_id INT NOT NULL, PRIMARY KEY (player_uuid, group_id), FOREIGN KEY (group_id) REFERENCES permission_groups(id) ON DELETE CASCADE);", "创建 player_groups 表");

    // 使用数据库方言方法添加列
    executeAndLog(db_->getAddColumnSql("permission_groups", "priority", "INT NOT NULL DEFAULT 0"), "向 permission_groups 添加 priority 列 (可能已存在)");

    // --- 添加索引以优化查询 ---
    logger.debug("尝试创建索引 (如果不存在)...");

    // 使用数据库方言方法创建索引
    executeAndLog(db_->getCreateIndexSql("idx_permissions_name", "permissions", "name"), "为 permissions.name 创建索引 idx_permissions_name");
    executeAndLog(db_->getCreateIndexSql("idx_permission_groups_name", "permission_groups", "name"), "为 permission_groups.name 创建索引 idx_permission_groups_name");
    executeAndLog(db_->getCreateIndexSql("idx_player_groups_uuid", "player_groups", "player_uuid"), "为 player_groups.player_uuid 创建索引 idx_player_groups_uuid");

    logger.debug("完成创建索引尝试。");
    // --- 结束添加索引 ---

    logger.debug("完成确保权限表的操作。");
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

// 新增辅助函数：从数据库获取组 ID (无锁版本)
string PermissionManager::_getGroupIdFromDb(const string& groupName) {
    if (!db_) {
        ::ll::mod::NativeMod::current()->getLogger().error("在 _getGroupIdFromDb 中数据库未初始化，组名 '%s'", groupName.c_str());
        return "";
    }
    string sql = "SELECT id FROM permission_groups WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(sql, {groupName});
    if (rows.empty() || rows[0].empty()) {
        return "";
    }
    return rows[0][0];
}

// --- 缓存实现 ---

void PermissionManager::populateGroupCache() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    try {
        logger.debug("正在填充权限组缓存...");
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
        logger.debug("权限组缓存已填充，共 %zu 个条目。", groupNameCache_.size());
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

    // 4. 缓存中确实没有，查询数据库，使用无锁的 _getGroupIdFromDb
    string groupId = _getGroupIdFromDb(groupName);

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
        logger.debug("正在填充组权限缓存...");
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
                    string gid = _getGroupIdFromDb(currentGroupName); // 直接从数据库获取组 ID (无锁)
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
        logger.debug("组权限缓存已填充，共 %zu 个条目。", groupPermissionsCache_.size());
    } catch (const exception& e) {
        logger.error("填充组权限缓存时异常: %s", e.what());
        std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
        groupPermissionsCache_.clear();
    }
}

void PermissionManager::invalidateGroupPermissionsCache(const string& groupName) {
    // 将任务推入队列，而不是直接失效
    enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
    ::ll::mod::NativeMod::current()->getLogger().debug("组 '%s' 的权限缓存失效任务已入队。", groupName.c_str());
}

void PermissionManager::invalidateAllGroupPermissionsCache() {
    // 将任务推入队列，而不是直接失效
    enqueueTask({CacheInvalidationTaskType::ALL_GROUPS_MODIFIED, ""});
    ::ll::mod::NativeMod::current()->getLogger().debug("所有组的权限缓存失效任务已入队。");
}

// --- 结束缓存实现 ---


bool PermissionManager::registerPermission(const string& name, const string& description, bool defaultValue) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("注册权限 '%s'，默认值 %s", name.c_str(), defaultValue ? "true" : "false");
    string defaultValueStr = defaultValue ? "1" : "0";

    // 先尝试插入，使用数据库方言的插入或忽略冲突语句
    string insertSql = db_->getInsertOrIgnoreSql(
        "permissions",
        "name, description, default_value",
        "?, ?, ?",
        "name" // 冲突列
    );
    bool insertOk = db_->executePrepared(insertSql, {name, description, defaultValueStr});

    // 然后更新 (以防它已经存在)
    string updateSql = "UPDATE permissions SET description = ?, default_value = ? WHERE name = ?;";
    bool updateOk = db_->executePrepared(updateSql, {description, defaultValueStr, name});

    // 如果任一操作逻辑上成功（插入新的或更新现有的），则认为成功
    // 更健壮的检查可能涉及检查受影响的行数（如果数据库 API 支持）。
    // 目前，如果 execute 调用没有返回 false（表示数据库错误），我们假设成功。
    if (insertOk && updateOk) {
        // 权限注册/更新可能影响所有玩家的默认权限，这里将任务推入队列
        enqueueTask({CacheInvalidationTaskType::ALL_PLAYERS_MODIFIED, ""});
        // 注册新权限也可能影响所有组的通配符权限，因此需要使所有组权限缓存失效
        enqueueTask({CacheInvalidationTaskType::ALL_GROUPS_MODIFIED, ""});
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
    logger.debug("尝试创建组 '%s'...", groupName.c_str());

    // 使用数据库方言的插入或忽略冲突语句
    string insertSql = db_->getInsertOrIgnoreSql(
        "permission_groups",
        "name, description",
        "?, ?",
        "name" // 冲突列
    );

    // Execute the insert statement. We don't need the returned ID here,
    // we will query for it by name afterwards.
    bool insertAttemptSuccess = db_->executePrepared(insertSql, {groupName, description});

    // Even if insertAttemptSuccess is false (e.g., a real DB error, not just conflict/ignore),
    // or if it was ignored, we now query by name to get the ID.
    // This handles both cases: group was newly created, or group already existed.
    string gid = getIdByName("permission_groups", groupName);

    if (!gid.empty()) {
        // Successfully got the ID, either from a new insert or an existing group
        logger.debug("组 '%s' 已存在或已创建 (ID: %s)。", groupName.c_str(), gid.c_str());
        updateGroupCache(groupName, gid); // Update group name cache
        invalidateGroupPermissionsCache(groupName); // Invalidate this group's permission cache
        logger.debug("组 '%s' (ID: %s) 缓存已更新。", groupName.c_str(), gid.c_str());
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
    logger.debug("尝试删除组 '%s'...", groupName.c_str());

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

    // 开始事务
    if (!db_->beginTransaction()) {
        logger.error("删除组 '%s' 时无法开始事务。", groupName.c_str());
        return false;
    }

    string sql = "DELETE FROM permission_groups WHERE id = ?;";
    bool success = db_->executePrepared(sql, {gid});

    if (success) {
        // 提交事务
        if (db_->commit()) {
            invalidateGroupCache(groupName); // 如果删除成功，则使组名缓存失效
            // 将 GROUP_MODIFIED 任务推入队列，由工作线程处理后续的子组和玩家失效
            enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
            logger.debug("删除组 '%s' (ID: %s) 成功，相关缓存失效任务已入队。", groupName.c_str(), gid.c_str());
        } else {
            logger.error("删除组 '%s' (ID: %s) 时提交事务失败，正在回滚。", groupName.c_str(), gid.c_str());
            db_->rollback(); // 提交失败则回滚
            success = false;
        }
    } else {
        logger.debug("删除组 '%s' (ID: %s) 失败，正在回滚。", groupName.c_str(), gid.c_str());
        db_->rollback(); // 删除失败则回滚
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

    // 使用数据库方言的插入或忽略冲突语句
    string insertSql = db_->getInsertOrIgnoreSql(
        "group_permissions",
        "group_id, permission_rule",
        "?, ?",
        "group_id, permission_rule" // 冲突列
    );
    bool success = db_->executePrepared(insertSql, {gid, permissionRule});
    if (success) {
        // 组权限变化，将 GROUP_MODIFIED 任务推入队列
        enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        logger.debug("向组 '%s' (GID: %s) 添加权限规则 '%s' 成功，缓存失效任务已入队。",
                    groupName.c_str(), gid.c_str(), permissionRule.c_str());
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
    logger.debug("从组 '%s' (GID: %s) 移除权限规则 '%s'", permissionRule.c_str(), groupName.c_str(), gid.c_str());

    string sql     = "DELETE FROM group_permissions WHERE group_id = ? AND permission_rule = ?;";
    bool success = db_->executePrepared(sql, {gid, permissionRule});
    if (success) {
        // 组权限变化，将 GROUP_MODIFIED 任务推入队列
        enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        logger.debug("从组 '%s' (GID: %s) 移除权限规则 '%s' 成功，缓存失效任务已入队。",
                    groupName.c_str(), gid.c_str(), permissionRule.c_str());
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
    auto&  logger = ::ll::mod::NativeMod::current()->getLogger();
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
    // 使用数据库方言的插入或忽略冲突语句
    string insertSql = db_->getInsertOrIgnoreSql(
        "group_inheritance",
        "group_id, parent_group_id",
        "?, ?",
        "group_id, parent_group_id" // 冲突列
    );
    bool success = db_->executePrepared(insertSql, {gid, pgid});
    if (success) {
        // 继承关系变化，将 GROUP_MODIFIED 任务推入队列
        enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        logger.debug("添加组 '%s' 继承 '%s' 成功，缓存失效任务已入队。", groupName.c_str(), parentGroupName.c_str());
    }
    return success;
}

bool PermissionManager::removeGroupInheritance(const string& groupName, const string& parentGroupName) {
    auto&  logger = ::ll::mod::NativeMod::current()->getLogger();
    string gid = getCachedGroupId(groupName); // 使用缓存
    string pgid = getCachedGroupId(parentGroupName); // 使用缓存
    if (gid.empty() || pgid.empty()) {
        return false; // 尝试移除不存在的映射 (来自缓存或数据库) 时不发出警告
    }
    string sql = "DELETE FROM group_inheritance WHERE group_id = ? AND parent_group_id = ?;";
    bool success = db_->executePrepared(sql, {gid, pgid});
    if (success) {
        // 继承关系变化，将 GROUP_MODIFIED 任务推入队列
        enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        logger.debug("移除组 '%s' 继承 '%s' 成功，缓存失效任务已入队。", groupName.c_str(), parentGroupName.c_str());
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
        string gid = _getGroupIdFromDb(currentGroupName); // 直接从数据库获取组 ID (无锁)
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
    logger.debug("将玩家 '%s' 添加到组 '%s'", playerUuid.c_str(), groupName.c_str());
    string gid = getCachedGroupId(groupName); // 使用缓存
    if (gid.empty()) {
        logger.warn("AddPlayerToGroup: 组 '%s' 未找到 (来自缓存或数据库)。", groupName.c_str());
        return false;
    }
    // 使用数据库方言的插入或忽略冲突语句
    string insertSql = db_->getInsertOrIgnoreSql(
        "player_groups",
        "player_uuid, group_id",
        "?, ?",
        "player_uuid, group_id" // 冲突列
    );
    bool success = db_->executePrepared(insertSql, {playerUuid, gid});
    if (success) {
        enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid}); // 玩家组变化，将任务推入队列
        logger.debug("将玩家 '%s' 添加到组 '%s' 成功，缓存失效任务已入队。", playerUuid.c_str(), groupName.c_str());
    }
    return success;
}

bool PermissionManager::removePlayerFromGroup(const string& playerUuid, const string& groupName) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("从组 '%s' 移除玩家 '%s'", playerUuid.c_str(), groupName.c_str());
    string gid = getCachedGroupId(groupName); // 使用缓存
     if (gid.empty()) {
        // 尝试移除时，如果组不存在 (来自缓存或数据库)，则不发出警告
        return false;
    }
    string sql = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";
    bool success = db_->executePrepared(sql, {playerUuid, gid});
    if (success) {
        enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid}); // 玩家组变化，将任务推入队列
        logger.debug("从组 '%s' 移除玩家 '%s' 成功，缓存失效任务已入队。", playerUuid.c_str(), groupName.c_str());
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
    string gid = _getGroupIdFromDb(groupName); // 直接从数据库获取组 ID (无锁)
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

// 使特定玩家的权限缓存失效 (现在将任务推入队列)
void PermissionManager::invalidatePlayerPermissionsCache(const string& playerUuid) {
    enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
    ::ll::mod::NativeMod::current()->getLogger().debug("玩家 '%s' 的权限缓存失效任务已入队。", playerUuid.c_str());
}

// 使所有玩家的权限缓存失效 (现在将任务推入队列)
void PermissionManager::invalidateAllPlayerPermissionsCache() {
    enqueueTask({CacheInvalidationTaskType::ALL_PLAYERS_MODIFIED, ""});
    ::ll::mod::NativeMod::current()->getLogger().debug("所有玩家的权限缓存失效任务已入队。");
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


std::map<std::string, bool> PermissionManager::getAllPermissionsForPlayer(const std::string& playerUuid) {
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
    std::map<std::string, bool> effectivePermissions; // 存储最终解析后的权限状态

    // 1. 收集默认权限规则
    string defaultPermsSql = "SELECT name FROM permissions WHERE default_value = 1;";
    auto defaultRows = db_->queryPrepared(defaultPermsSql, {});
    for (const auto& row : defaultRows) {
        if (!row.empty()) {
            effectivePermissions[row[0]] = true; // 默认权限为授予
            logger.debug("玩家 '%s' 初始拥有默认规则: %s", playerUuid.c_str(), row[0].c_str());
        }
    }

    // 2. 获取玩家的组并按优先级排序
    auto groups = getPlayerGroups(playerUuid);
    if (!groups.empty()) {
        struct GroupInfo { string name; int priority; };
        vector<GroupInfo> playerGroupInfos;
        playerGroupInfos.reserve(groups.size());
        for (const auto& groupName : groups) {
            playerGroupInfos.push_back({groupName, getGroupPriority(groupName)});
        }
        // 按照优先级从低到高排序，以便高优先级的规则后应用
        sort(playerGroupInfos.begin(), playerGroupInfos.end(),
                  [](const GroupInfo& a, const GroupInfo& b) { return a.priority < b.priority; });

        logger.debug("玩家 '%s' 的组按优先级排序 (从低到高):", playerUuid.c_str());
        for(const auto& gi : playerGroupInfos) { logger.debug("- 组: %s, 优先级: %d", gi.name.c_str(), gi.priority); }

        // 3. 按优先级顺序收集所有组的规则并进行解析
        for (const auto& groupInfo : playerGroupInfos) {
            // getPermissionsOfGroup 返回此组的已解析规则（包括继承的），并利用了组权限缓存
            // 这些规则已经是 "压平" 后的，例如 "perm.a" 或 "-perm.b"
            auto groupRules = getPermissionsOfGroup(groupInfo.name);
            logger.debug("从组 '%s' (优先级 %d) 收集并解析规则: %zu 条规则", groupInfo.name.c_str(), groupInfo.priority, groupRules.size());
            for (const auto& rule : groupRules) {
                 if (rule.empty()) continue;

                 bool isNegatedRule = (rule[0] == '-');
                 string baseName = isNegatedRule ? rule.substr(1) : rule;

                 if (baseName.empty()) continue;

                 if (isNegatedRule) {
                     effectivePermissions[baseName] = false; // 否定规则覆盖
                     logger.debug("  解析规则 '%s': 将 '%s' 的状态设置为否定。", rule.c_str(), baseName.c_str());
                 } else {
                     effectivePermissions[baseName] = true; // 肯定规则覆盖
                     logger.debug("  解析规则 '%s': 将 '%s' 的状态设置为授予。", rule.c_str(), baseName.c_str());
                 }
            }
        }
    } else {
        logger.debug("玩家 '%s' 不属于任何组。", playerUuid.c_str());
    }

    // 4. 将结果存储到缓存
    {
        std::unique_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
        playerPermissionsCache_[playerUuid] = effectivePermissions;
        logger.debug("玩家 '%s' 的权限已缓存，共 %zu 条有效权限。", playerUuid.c_str(), effectivePermissions.size());
    }

    return effectivePermissions;
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
        // 优先级变化，将 GROUP_MODIFIED 任务推入队列
        enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        logger.debug("为组 '%s' 设置优先级 %d 成功，缓存失效任务已入队。", groupName.c_str(), priority);
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

    // 获取玩家的所有有效权限（这将利用缓存，返回已解析的 map）
    std::map<std::string, bool> playerEffectivePermissions = getAllPermissionsForPlayer(playerUuid);

    // 1. 首先检查精确匹配
    auto itExact = playerEffectivePermissions.find(permissionNode);
    if (itExact != playerEffectivePermissions.end()) {
        logger.debug("权限 '%s' 被玩家 '%s' 的精确规则 %s。",
                     permissionNode.c_str(),
                     playerUuid.c_str(),
                     itExact->second ? "授予" : "拒绝");
        return itExact->second; // 找到精确匹配，直接返回其状态
    }

    // 2. 如果没有精确匹配，则进行通配符匹配
    // 遍历 map 中的所有权限规则，寻找通配符匹配
    // 注意：map 是有序的，但通配符匹配的优先级需要根据规则的特异性来决定。
    // 考虑到 getAllPermissionsForPlayer 已经处理了优先级并压平了规则，
    // 这里的遍历顺序不直接决定优先级，而是检查是否存在匹配的规则。
    // 权限系统通常是“最具体规则优先”，但这里我们简化为“找到即返回”。
    // 如果存在多个通配符规则匹配，且有肯定有否定，则需要更复杂的逻辑。
    // 按照当前设计，getAllPermissionsForPlayer 已经将所有规则压平，
    // 所以这里只需要找到第一个匹配的通配符规则即可。
    // 更好的做法是，通配符匹配也应该从最具体的规则开始匹配。
    // 但由于 map 的键是按字典序排序的，我们不能直接依赖迭代顺序来判断优先级。
    // 因此，我们仍然需要遍历所有规则，并根据找到的规则来决定最终状态。
    // 为了保持与旧逻辑的“倒序遍历”以处理优先级（尽管现在优先级已在 getAllPermissionsForPlayer 中处理），
    // 我们需要确保通配符匹配的逻辑是正确的。
    // 实际上，如果 getAllPermissionsForPlayer 已经返回了最终的有效权限，
    // 那么 hasPermission 只需要检查这个 map。
    // 如果 map 中没有精确匹配，那么通配符匹配也应该基于这个 map 中的键。

    // 重新考虑通配符匹配的逻辑：
    // 玩家的有效权限 map 已经包含了所有经过优先级处理后的最终权限状态。
    // 如果 `permissionNode` 没有精确匹配，我们需要检查 `playerEffectivePermissions` 中是否存在通配符规则能够匹配 `permissionNode`。
    // 并且，我们需要找到最具体的匹配。
    // 这是一个复杂的问题，因为 `std::map` 不支持高效的通配符查找。
    // 原始的 `hasPermission` 是通过遍历 `vector<string>` 并对每个规则进行 `regex_match` 来实现的。
    // 优化后，`playerEffectivePermissions` 是一个 `map<string, bool>`，键是已经解析好的权限节点（不含负号）。
    // 所以，我们需要遍历 `map` 的键，将它们转换为正则表达式，然后与 `permissionNode` 进行匹配。

    // 默认值：如果没有任何规则匹配，则使用权限的默认值。
    bool finalPermissionState = false; // 默认拒绝

    // 检查权限的默认值
    string defaultSql = "SELECT default_value FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(defaultSql, {permissionNode});
    if (!rows.empty() && !rows[0].empty()) {
        try {
            finalPermissionState = stoi(rows[0][0]) != 0;
            logger.debug("权限 '%s' 初始默认值: %s", permissionNode.c_str(), finalPermissionState ? "true" : "false");
        } catch (const invalid_argument& ia) {
             logger.error("权限 '%s' 的 default_value 无效: %s", permissionNode.c_str(), rows[0][0].c_str());
        } catch (const out_of_range& oor) {
             logger.error("权限 '%s' 的 Default_value 超出范围: %s", permissionNode.c_str(), rows[0][0].c_str());
        }
    } else {
         logger.debug("在 permissions 表中未找到权限节点 '%s'，使用默认拒绝。", permissionNode.c_str());
    }

    // 遍历缓存的有效权限，进行通配符匹配
    // 这里的逻辑需要确保“最具体规则优先”的原则。
    // 由于 map 是按键排序的，我们不能直接依赖迭代顺序。
    // 更好的方法是，在 getAllPermissionsForPlayer 中，将通配符规则也解析成某种可快速匹配的结构，
    // 或者在 hasPermission 中，对所有匹配的通配符规则进行优先级判断。
    // 考虑到当前 getAllPermissionsForPlayer 已经将规则压平，
    // 这里的 map 键是 "perm.a" 或 "perm.*" 这样的形式。
    // 我们需要找到所有能匹配 `permissionNode` 的键，然后根据它们的 `bool` 值来决定。
    // 这是一个“最具体匹配优先”的问题。
    // 简单的实现是：找到所有匹配的规则，然后根据规则的长度（更长更具体）来决定。

    // 存储所有匹配的规则及其状态
    std::map<int, bool> matchingRulesBySpecificity; // 长度 -> 状态 (true/false)

    for (const auto& pair : playerEffectivePermissions) {
        const string& cachedPermissionPattern = pair.first; // 例如 "my.perm" 或 "my.*"
        bool cachedState = pair.second; // true (授予) 或 false (否定)

        // 将缓存的权限模式转换为正则表达式
        string regexPatternStr = wildcardToRegex(cachedPermissionPattern);

        try {
            std::regex permissionRegex(regexPatternStr);
            if (std::regex_match(permissionNode, permissionRegex)) {
                // 匹配成功，记录规则的长度和状态
                // 规则越长，通常认为越具体
                matchingRulesBySpecificity[cachedPermissionPattern.length()] = cachedState;
                logger.debug("通配符匹配：权限 '%s' 匹配规则 '%s' (%s)。",
                             permissionNode.c_str(),
                             cachedPermissionPattern.c_str(),
                             cachedState ? "授予" : "拒绝");
            }
        } catch (const std::regex_error& e) {
             logger.error("从缓存规则 '%s' 生成的无效正则表达式模式: %s", cachedPermissionPattern.c_str(), e.what());
             // 跳过此无效规则
        }
    }

    // 如果有匹配的通配符规则，则取最具体的（长度最长）规则的状态
    if (!matchingRulesBySpecificity.empty()) {
        // map 会自动按键（长度）排序，所以最后一个元素就是最长的规则
        finalPermissionState = matchingRulesBySpecificity.rbegin()->second;
        logger.debug("权限 '%s' 通过最具体通配符规则决定为: %s",
                     permissionNode.c_str(),
                     finalPermissionState ? "授予" : "拒绝");
    }

    return finalPermissionState;
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
        logger.debug("成功更新组 '%s' (ID: %s) 的描述。", groupName.c_str(), gid.c_str());
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
