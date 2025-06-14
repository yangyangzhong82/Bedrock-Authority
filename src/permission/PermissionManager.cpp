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

bool PermissionManager::init(db::IDatabase* db) {
    db_ = db;
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    try {
        ensureTables();
        populateGroupCache(); // 初始化时填充组名缓存
        populateGroupPermissionsCache(); // 初始化时填充组权限缓存
        populateInheritanceCache(); // 初始化时填充继承图缓存
        logger.info("权限管理器已初始化并填充了组缓存、组权限缓存和继承图缓存");

        // 启动异步任务工作线程
        running_ = true;
        workerThread_ = std::thread(&PermissionManager::processTasks, this);
        logger.info("权限管理器异步缓存失效工作线程已启动。");
        return true;
    } catch (const std::exception& e) {
        logger.error("权限管理器初始化失败: %s", e.what());
        return false;
    } catch (...) {
        logger.error("权限管理器初始化失败: 未知错误。");
        return false;
    }
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
        std::unique_lock<std::mutex> pendingLock(pendingTasksMutex_); // 保护任务合并相关成员

        if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
            // 如果 ALL_GROUPS_MODIFIED 任务已在队列中，则无需添加 GROUP_MODIFIED 任务
            if (allGroupsModifiedPending_) {
                ::ll::mod::NativeMod::current()->getLogger().debug("GROUP_MODIFIED 任务 '%s' 被合并，因为 ALL_GROUPS_MODIFIED 任务已在队列中。", task.data.c_str());
                return;
            }
            // 如果相同组名的 GROUP_MODIFIED 任务已在队列中，则无需重复添加
            if (pendingGroupModifiedTasks_.count(task.data)) {
                ::ll::mod::NativeMod::current()->getLogger().debug("GROUP_MODIFIED 任务 '%s' 被合并，因为相同任务已在队列中。", task.data.c_str());
                return;
            }
            pendingGroupModifiedTasks_.insert(task.data);
        } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
            // 如果 ALL_GROUPS_MODIFIED 任务已在队列中，则无需重复添加
            if (allGroupsModifiedPending_) {
                ::ll::mod::NativeMod::current()->getLogger().debug("ALL_GROUPS_MODIFIED 任务被合并，因为相同任务已在队列中。");
                return;
            }
            // 如果添加 ALL_GROUPS_MODIFIED 任务，则清除所有待处理的 GROUP_MODIFIED 任务
            // 并从实际任务队列中移除它们（如果可能的话，这里需要更复杂的队列操作，
            // 但由于 std::queue 不支持随机访问，我们只能通过标记来处理）
            // 简单起见，这里我们只清除 pendingGroupModifiedTasks_，并设置 allGroupsModifiedPending_ 标志
            pendingGroupModifiedTasks_.clear();
            allGroupsModifiedPending_ = true;
            ::ll::mod::NativeMod::current()->getLogger().debug("ALL_GROUPS_MODIFIED 任务已入队，并清除了所有待处理的 GROUP_MODIFIED 任务。");
        }

        taskQueue_.push(std::move(task));
    } // 锁在此处释放
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
        } // queueMutex_ 锁在此处释放

        // 在处理任务之前，从 pending 集合中移除对应的任务
        {
            std::unique_lock<std::mutex> pendingLock(pendingTasksMutex_);
            if (task.type == CacheInvalidationTaskType::GROUP_MODIFIED) {
                pendingGroupModifiedTasks_.erase(task.data);
            } else if (task.type == CacheInvalidationTaskType::ALL_GROUPS_MODIFIED) {
                allGroupsModifiedPending_ = false; // 处理完 ALL_GROUPS_MODIFIED 任务后重置标志
            }
        } // pendingTasksMutex_ 锁在此处释放

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
                    // ALL_GROUPS_MODIFIED 任务会影响所有玩家的权限，因此也需要失效所有玩家的权限缓存
                    _invalidateAllPlayerPermissionsCache();
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

        groupPermissionsCache_.clear(); // 清除旧缓存 (在循环外，不持有锁)

        for (const auto& row : rows) {
            if (!row.empty() && !row[0].empty()) {
                string groupName = row[0];
                try {
                    // 直接调用 getPermissionsOfGroup，它会自行处理其内部逻辑并返回 CompiledPermissionRule 列表
                    // getPermissionsOfGroup 内部会负责将结果写入缓存，并处理其自身的锁
                    getPermissionsOfGroup(groupName);
                } catch (const std::exception& e) {
                    logger.error("为组 '%s' 填充权限缓存时异常: %s", groupName.c_str(), e.what());
                } catch (...) {
                    logger.error("为组 '%s' 填充权限缓存时发生未知异常。", groupName.c_str());
                }
            }
        }
        // 在所有 getPermissionsOfGroup 调用完成后，再获取锁并检查缓存大小
        // 确保 groupPermissionsCache_ 的大小是准确的
        {
            std::shared_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
            logger.debug("组权限缓存已填充，共 %zu 个条目。", groupPermissionsCache_.size());
        }
    } catch (const exception& e) {
        logger.error("填充组权限缓存时异常: %s", e.what());
        // 即使发生异常，也应该清除缓存，但不需要在这里获取独占锁，因为异常处理可能在其他地方
        // 并且这里只是一个填充函数，如果失败，后续的 getPermissionsOfGroup 会重新计算并填充
        // 或者在 init 失败时直接返回 false
        // 为了安全，还是在异常时清除，但要确保锁的正确性
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

void PermissionManager::populateInheritanceCache() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    try {
        logger.debug("正在填充继承图缓存...");
        string sql = "SELECT T1.name AS child_name, T2.name AS parent_name "
                     "FROM group_inheritance gi "
                     "JOIN permission_groups T1 ON gi.group_id = T1.id "
                     "JOIN permission_groups T2 ON gi.parent_group_id = T2.id;";
        auto rows = db_->queryPrepared(sql, {});

        std::unique_lock<std::shared_mutex> lock(inheritanceCacheMutex_);
        parentToChildren_.clear();
        childToParents_.clear();

        for (const auto& row : rows) {
            if (row.size() >= 2 && !row[0].empty() && !row[1].empty()) {
                string childName = row[0];
                string parentName = row[1];
                parentToChildren_[parentName].insert(childName);
                childToParents_[childName].insert(parentName);
            }
        }
        logger.debug("继承图缓存已填充，共 %zu 条父子关系。", parentToChildren_.size() + childToParents_.size());
    } catch (const exception& e) {
        logger.error("填充继承图缓存时异常: %s", e.what());
        std::unique_lock<std::shared_mutex> lock(inheritanceCacheMutex_);
        parentToChildren_.clear();
        childToParents_.clear();
    }
}

void PermissionManager::updateInheritanceCache(const string& groupName, const string& parentGroupName) {
    std::unique_lock<std::shared_mutex> lock(inheritanceCacheMutex_);
    childToParents_[groupName].insert(parentGroupName);
    parentToChildren_[parentGroupName].insert(groupName);
}

void PermissionManager::removeInheritanceFromCache(const string& groupName, const string& parentGroupName) {
    std::unique_lock<std::shared_mutex> lock(inheritanceCacheMutex_);
    if (childToParents_.count(groupName)) {
        childToParents_[groupName].erase(parentGroupName);
        if (childToParents_[groupName].empty()) {
            childToParents_.erase(groupName);
        }
    }
    if (parentToChildren_.count(parentGroupName)) {
        parentToChildren_[parentGroupName].erase(groupName);
        if (parentToChildren_[parentGroupName].empty()) {
            parentToChildren_.erase(parentGroupName);
        }
    }
}

// 新增辅助函数：检查继承图中是否存在从 startNode 到 endNode 的路径 (用于循环检测)
bool PermissionManager::hasPath(const std::string& startNode, const std::string& endNode) {
    if (startNode == endNode) {
        return true; // 如果起始节点和目标节点相同，则存在路径（自循环）
    }

    std::queue<std::string> q;
    std::set<std::string> visited;

    {
        std::shared_lock<std::shared_mutex> lock(inheritanceCacheMutex_); // 保护继承图缓存的读取

        // 检查起始节点是否存在于缓存中
        if (parentToChildren_.count(startNode)) {
            q.push(startNode);
            visited.insert(startNode);

            while (!q.empty()) {
                std::string current = q.front();
                q.pop();

                // 检查当前节点的子节点
                auto it = parentToChildren_.find(current);
                if (it != parentToChildren_.end()) {
                    for (const std::string& neighbor : it->second) {
                        if (neighbor == endNode) {
                            return true; // 找到路径
                        }
                        if (visited.find(neighbor) == visited.end()) {
                            visited.insert(neighbor);
                            q.push(neighbor);
                        }
                    }
                }
            }
        }
    } // 锁在此处释放

    return false; // 未找到路径
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
            // 从继承图缓存中移除所有与该组相关的继承关系
            std::unique_lock<std::shared_mutex> lock(inheritanceCacheMutex_);
            if (parentToChildren_.count(groupName)) {
                for (const string& child : parentToChildren_[groupName]) {
                    if (childToParents_.count(child)) {
                        childToParents_[child].erase(groupName);
                        if (childToParents_[child].empty()) {
                            childToParents_.erase(child);
                        }
                    }
                }
                parentToChildren_.erase(groupName);
            }
            if (childToParents_.count(groupName)) {
                for (const string& parent : childToParents_[groupName]) {
                    if (parentToChildren_.count(parent)) {
                        parentToChildren_[parent].erase(groupName);
                        if (parentToChildren_[parent].empty()) {
                            parentToChildren_.erase(parent);
                        }
                    }
                }
                childToParents_.erase(groupName);
            }
            lock.unlock(); // 提前释放锁

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
    std::shared_lock<std::shared_mutex> lock(inheritanceCacheMutex_);
    auto it = childToParents_.find(groupName);
    if (it != childToParents_.end()) {
        for (const string& parent : it->second) {
            parents.push_back(parent);
        }
    }
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

    // 添加循环检测：在尝试添加 child -> parent 这条边之前，
    // 检查从 parent 节点开始，在当前的继承图（内存缓存的图）中进行一次 DFS 或 BFS 遍历。
    // 检查在遍历过程中是否能到达 child 节点。
    // 如果能，说明添加这条边会形成一个环，应立即返回 false 并向用户报告错误。
    if (hasPath(parentGroupName, groupName)) {
        logger.warn("AddGroupInheritance: 添加继承关系 '%s' -> '%s' 会形成循环，操作被阻止。", groupName.c_str(), parentGroupName.c_str());
        return false;
    }

    // 使用数据库方言的插入或忽略冲突语句
    string insertSql = db_->getInsertOrIgnoreSql(
        "group_inheritance",
        "group_id, parent_group_id",
        "?, ?",
        "group_id, parent_group_id" // 冲突列
    );
    bool success = db_->executePrepared(insertSql, {gid, pgid});
    if (success) {
        updateInheritanceCache(groupName, parentGroupName); // 更新继承图缓存
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
        removeInheritanceFromCache(groupName, parentGroupName); // 从继承图缓存中移除
        // 继承关系变化，将 GROUP_MODIFIED 任务推入队列
        enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        logger.debug("移除组 '%s' 继承 '%s' 成功，缓存失效任务已入队。", groupName.c_str(), parentGroupName.c_str());
    }
    return success;
}

// #include <map> // 为解析步骤包含 map (已在文件顶部包含)

std::vector<CompiledPermissionRule> PermissionManager::getPermissionsOfGroup(const std::string& groupName) {
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
    std::vector<std::string> allRules; // 首先在这里收集所有原始规则字符串
    std::set<std::string> visited;     // 防止继承中的循环

    std::function<void(const std::string&)> dfs = [&](const std::string& currentGroupName) {
        if (visited.count(currentGroupName)) return;
        visited.insert(currentGroupName);

        logger.debug("DFS: 正在获取组 '%s' 的直接权限规则。", currentGroupName.c_str());
        // 获取当前组的直接权限规则
        std::string gid = _getGroupIdFromDb(currentGroupName); // 直接从数据库获取组 ID (无锁)
        if (!gid.empty()) {
            std::string directRulesSql = "SELECT permission_rule FROM group_permissions WHERE group_id = ?;";
            logger.debug("DFS: 正在执行 SQL 查询直接权限: %s (GID: %s)", directRulesSql.c_str(), gid.c_str());
            auto directRows = db_->queryPrepared(directRulesSql, {gid});
            logger.debug("DFS: 查询直接权限返回 %zu 行。", directRows.size());
            for (auto& row : directRows) {
                if (!row.empty() && !row[0].empty()) { // 确保行和规则字符串不为空
                    // 直接将原始规则字符串添加到列表中
                    allRules.push_back(row[0]);
                }
            }
        } else {
            logger.warn("getPermissionsOfGroup (DFS): 获取直接规则时未找到组 '%s'。", currentGroupName.c_str());
        }

        logger.debug("DFS: 正在获取组 '%s' 的父组。", currentGroupName.c_str());
        // 递归地从父组获取权限
        auto parentGroups = getParentGroups(currentGroupName);
        logger.debug("DFS: 组 '%s' 有 %zu 个父组。", currentGroupName.c_str(), parentGroups.size());
        for (const auto& parentGroup : parentGroups) {
            dfs(parentGroup);
        }
    };

    dfs(groupName);
    logger.debug("DFS 完成，共收集到 %zu 条原始权限规则。", allRules.size());

    // --- 解析步骤 ---
    std::map<std::string, bool> effectiveState; // true = 授予, false = 否定

    logger.debug("开始解析权限规则...");
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
    logger.debug("权限规则解析完成，共 %zu 条有效权限。", effectiveState.size());

    // 从解析后的状态构建最终列表，并编译正则表达式
    std::vector<CompiledPermissionRule> finalCompiledPerms;
    logger.debug("开始编译正则表达式...");
    for (const auto& pair : effectiveState) {
        try {
            logger.debug("正在编译规则: '%s'", pair.first.c_str());
            finalCompiledPerms.emplace_back(pair.first, wildcardToRegex(pair.first), pair.second);
        } catch (const std::regex_error& e) {
            logger.error("编译权限规则 '%s' 时异常: %s", pair.first.c_str(), e.what());
            // 可以在这里选择跳过此规则或以其他方式处理错误
        }
    }
    logger.debug("正则表达式编译完成，共 %zu 条编译规则。", finalCompiledPerms.size());

    // 可选：排序以保持一致性，或者根据特异性排序（更长的模式更具体）
    // 这里我们按模式字符串长度降序排序，以便在 hasPermission 中优先匹配更具体的规则
    std::sort(finalCompiledPerms.begin(), finalCompiledPerms.end(),
              [](const CompiledPermissionRule& a, const CompiledPermissionRule& b) {
                  return a.pattern.length() > b.pattern.length();
              });
    logger.debug("编译规则已排序。");

    // 3. 将结果存储到缓存
    {
        std::unique_lock<std::shared_mutex> lock(groupPermissionsCacheMutex_);
        groupPermissionsCache_[groupName] = finalCompiledPerms;
        logger.debug("组 '%s' 的权限已缓存。", groupName.c_str());
    }

    return finalCompiledPerms; // 返回解析并编译后的列表
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

// 新增函数：一次性获取玩家所有组的名称和优先级
std::vector<GroupDetails> PermissionManager::getPlayerGroupsWithPriorities(const std::string& playerUuid) {
    std::vector<GroupDetails> playerGroupDetails;
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();

    // SQL 查询一次性获取组名和优先级
    std::string sql = "SELECT pg.id, pg.name, pg.description, pg.priority "
                      "FROM permission_groups pg "
                      "JOIN player_groups pgr ON pg.id = pgr.group_id "
                      "WHERE pgr.player_uuid = ?;";
    
    auto rows = db_->queryPrepared(sql, {playerUuid});

    for (const auto& row : rows) {
        if (row.size() >= 4) {
            try {
                int priority = std::stoi(row[3]);
                playerGroupDetails.emplace_back(row[0], row[1], row[2], priority);
            } catch (const std::invalid_argument& ia) {
                logger.error("getPlayerGroupsWithPriorities: 组 '%s' 的优先级值无效: %s", row[1].c_str(), row[3].c_str());
            } catch (const std::out_of_range& oor) {
                logger.error("getPlayerGroupsWithPriorities: 组 '%s' 的优先级值超出范围: %s", row[1].c_str(), row[3].c_str());
            }
        }
    }
    return playerGroupDetails;
}


// 辅助函数：将通配符模式转换为正则表达式对象
std::regex PermissionManager::wildcardToRegex(const std::string& pattern) {
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
    return std::regex(regexPatternStr, std::regex_constants::ECMAScript | std::regex_constants::icase); // 忽略大小写
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

// 新增辅助函数：递归获取所有子组 (现在使用缓存)
std::set<string> PermissionManager::getChildGroupsRecursive(const string& groupName) {
    std::set<string> allChildGroups;
    std::queue<string> q;

    {
        std::shared_lock<std::shared_mutex> lock(inheritanceCacheMutex_);
        // 检查起始组是否存在于缓存中
        if (parentToChildren_.count(groupName)) {
            q.push(groupName);
            allChildGroups.insert(groupName); // Include the starting group itself

            while (!q.empty()) {
                string currentGroup = q.front();
                q.pop();

                // 从缓存中获取直接子组
                auto it = parentToChildren_.find(currentGroup);
                if (it != parentToChildren_.end()) {
                    for (const string& childGroup : it->second) {
                        if (allChildGroups.find(childGroup) == allChildGroups.end()) {
                            allChildGroups.insert(childGroup);
                            q.push(childGroup);
                        }
                    }
                }
            }
        } else {
            // 如果起始组不在缓存中，它可能没有子组，或者缓存尚未完全填充
            // 为了安全起见，如果缓存中没有，我们仍然将其自身包含在内
            allChildGroups.insert(groupName);
        }
    } // 锁在此处释放

    return allChildGroups;
}

// 新增辅助函数：获取受特定组修改影响的所有玩家 UUID (现在使用缓存)
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
    // 现在 getChildGroupsRecursive 使用了缓存，所以这里会更快
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


std::vector<CompiledPermissionRule> PermissionManager::getAllPermissionsForPlayer(const std::string& playerUuid) {
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
    std::map<std::string, bool> effectiveStateMap; // 存储最终解析后的权限状态 (字符串 -> bool)

    // 1. 收集默认权限规则
    std::string defaultPermsSql = "SELECT name FROM permissions WHERE default_value = 1;";
    auto defaultRows = db_->queryPrepared(defaultPermsSql, {});
    for (const auto& row : defaultRows) {
        if (!row.empty()) {
            effectiveStateMap[row[0]] = true; // 默认权限为授予
            logger.debug("玩家 '%s' 初始拥有默认规则: %s", playerUuid.c_str(), row[0].c_str());
        }
    }

    // 2. 获取玩家的组并按优先级排序 (使用新的 getPlayerGroupsWithPriorities 函数)
    std::vector<GroupDetails> playerGroupInfos = getPlayerGroupsWithPriorities(playerUuid);
    if (!playerGroupInfos.empty()) {
        // 按照优先级从低到高排序，以便高优先级的规则后应用
        std::sort(playerGroupInfos.begin(), playerGroupInfos.end(),
                  [](const GroupDetails& a, const GroupDetails& b) { return a.priority < b.priority; });

        logger.debug("玩家 '%s' 的组按优先级排序 (从低到高):", playerUuid.c_str());
        for (const auto& gi : playerGroupInfos) {
            logger.debug("- 组: %s, 优先级: %d", gi.name.c_str(), gi.priority);
        }

        // 3. 按优先级顺序收集所有组的规则并进行解析
        for (const auto& groupInfo : playerGroupInfos) {
            // getPermissionsOfGroup 返回此组的已解析和编译的规则
            auto groupRules = getPermissionsOfGroup(groupInfo.name);
            logger.debug("从组 '%s' (优先级 %d) 收集并解析规则: %zu 条规则", groupInfo.name.c_str(), groupInfo.priority,
                         groupRules.size());
            for (const auto& rule : groupRules) {
                // rule.pattern 是原始字符串，rule.state 是解析后的状态
                if (rule.state) {
                    effectiveStateMap[rule.pattern] = true; // 肯定规则覆盖
                    logger.debug("  解析规则 '%s': 将 '%s' 的状态设置为授予。", rule.pattern.c_str(),
                                 rule.pattern.c_str());
                } else {
                    effectiveStateMap[rule.pattern] = false; // 否定规则覆盖
                    logger.debug("  解析规则 '%s': 将 '%s' 的状态设置为否定。", rule.pattern.c_str(),
                                 rule.pattern.c_str());
                }
            }
        }
    } else {
        logger.debug("玩家 '%s' 不属于任何组。", playerUuid.c_str());
    }

    // 将最终的 effectiveStateMap 转换为 CompiledPermissionRule 向量
    std::vector<CompiledPermissionRule> finalCompiledPerms;
    for (const auto& pair : effectiveStateMap) {
        try {
            finalCompiledPerms.emplace_back(pair.first, wildcardToRegex(pair.first), pair.second);
        } catch (const std::regex_error& e) {
            logger.error("编译玩家权限规则 '%s' 时异常: %s", pair.first.c_str(), e.what());
            // 可以在这里选择跳过此规则或以其他方式处理错误
        }
    }

    // 按照模式字符串长度降序排序，以便在 hasPermission 中优先匹配更具体的规则
    std::sort(finalCompiledPerms.begin(), finalCompiledPerms.end(),
              [](const CompiledPermissionRule& a, const CompiledPermissionRule& b) {
                  return a.pattern.length() > b.pattern.length();
              });

    // 4. 将结果存储到缓存
    {
        std::unique_lock<std::shared_mutex> lock(playerPermissionsCacheMutex_);
        playerPermissionsCache_[playerUuid] = finalCompiledPerms;
        logger.debug("玩家 '%s' 的权限已缓存，共 %zu 条有效权限。", playerUuid.c_str(), finalCompiledPerms.size());
    }

    return finalCompiledPerms;
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

bool PermissionManager::hasPermission(const std::string& playerUuid, const std::string& permissionNode) {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("检查玩家 '%s' 的权限 '%s'", playerUuid.c_str(), permissionNode.c_str());

    // 获取玩家的所有有效权限（这将利用缓存，返回已解析和编译的规则列表）
    std::vector<CompiledPermissionRule> playerEffectivePermissions = getAllPermissionsForPlayer(playerUuid);

    // 默认值：如果没有任何规则匹配，则使用权限的默认值。
    bool finalPermissionState = false; // 默认拒绝

    // 检查权限的默认值
    std::string defaultSql = "SELECT default_value FROM permissions WHERE name = ? LIMIT 1;";
    auto rows = db_->queryPrepared(defaultSql, {permissionNode});
    if (!rows.empty() && !rows[0].empty()) {
        try {
            finalPermissionState = std::stoi(rows[0][0]) != 0;
            logger.debug("权限 '%s' 初始默认值: %s", permissionNode.c_str(), finalPermissionState ? "true" : "false");
        } catch (const std::invalid_argument& ia) {
            logger.error("权限 '%s' 的 default_value 无效: %s", permissionNode.c_str(), rows[0][0].c_str());
        } catch (const std::out_of_range& oor) {
            logger.error("权限 '%s' 的 Default_value 超出范围: %s", permissionNode.c_str(), rows[0][0].c_str());
        }
    } else {
        logger.debug("在 permissions 表中未找到权限节点 '%s'，使用默认拒绝。", permissionNode.c_str());
    }

    // 遍历玩家的有效权限列表，该列表已按特异性（长度）降序排序
    // 因此，第一个匹配的规则就是最具体的规则
    for (const auto& rule : playerEffectivePermissions) {
        if (std::regex_match(permissionNode, rule.regex)) {
            logger.debug("权限 '%s' 匹配规则 '%s' (%s)。",
                         permissionNode.c_str(),
                         rule.pattern.c_str(),
                         rule.state ? "授予" : "拒绝");
            return rule.state; // 返回最具体匹配规则的状态
        }
    }

    // 如果没有找到任何匹配的规则（包括精确和通配符），则返回权限的默认状态
    logger.debug("权限 '%s' 未找到匹配规则，返回默认状态: %s",
                 permissionNode.c_str(),
                 finalPermissionState ? "授予" : "拒绝");
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
    auto&  logger = ::ll::mod::NativeMod::current()->getLogger();
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

size_t PermissionManager::addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames) {
    if (groupNames.empty()) {
        return 0;
    }

    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("批量操作：将玩家 '%s' 添加到 %zu 个组...", playerUuid.c_str(), groupNames.size());

    if (!db_->beginTransaction()) {
        logger.error("addPlayerToGroups 无法开始事务。");
        return 0;
    }

    size_t      successCount = 0;
    std::string insertSql =
        db_->getInsertOrIgnoreSql("player_groups", "player_uuid, group_id", "?, ?", "player_uuid, group_id");

    for (const auto& groupName : groupNames) {
        std::string gid = getCachedGroupId(groupName);
        if (gid.empty()) {
            logger.warn("addPlayerToGroups: 组 '%s' 未找到，跳过。", groupName.c_str());
            continue;
        }
        if (db_->executePrepared(insertSql, {playerUuid, gid})) {
            successCount++;
        }
    }

    if (db_->commit()) {
        if (successCount > 0) {
            // 只要有任何成功的操作，就认为玩家的组关系已改变
            enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
            logger.debug("批量添加玩家到组成功，提交了 %zu 个变更，缓存失效任务已入队。", successCount);
        }
    } else {
        logger.error("addPlayerToGroups 提交事务失败，正在回滚。");
        db_->rollback();
        return 0; // 提交失败，返回0表示操作未完全成功
    }

    return successCount;
}

size_t
PermissionManager::removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames) {
    if (groupNames.empty()) {
        return 0;
    }

    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("批量操作：从 %zu 个组中移除玩家 '%s'...", groupNames.size(), playerUuid.c_str());

    if (!db_->beginTransaction()) {
        logger.error("removePlayerFromGroups 无法开始事务。");
        return 0;
    }

    size_t      successCount = 0;
    std::string deleteSql    = "DELETE FROM player_groups WHERE player_uuid = ? AND group_id = ?;";

    for (const auto& groupName : groupNames) {
        std::string gid = getCachedGroupId(groupName);
        if (gid.empty()) {
            // 移除时如果组不存在，是正常情况，静默处理
            continue;
        }
        if (db_->executePrepared(deleteSql, {playerUuid, gid})) {
            // 注意：executePrepared 即使没有删除行也可能返回 true。
            // 如果需要精确计数，需要检查受影响的行数，但这会增加 API 复杂性。
            // 目前，我们假设执行成功即为一次成功的尝试。
            successCount++;
        }
    }

    if (db_->commit()) {
        if (successCount > 0) {
            enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
            logger.debug("批量移除玩家从组成功，提交了 %zu 个变更，缓存失效任务已入队。", successCount);
        }
    } else {
        logger.error("removePlayerFromGroups 提交事务失败，正在回滚。");
        db_->rollback();
        return 0;
    }

    return successCount;
}

size_t PermissionManager::addPermissionsToGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    if (permissionRules.empty()) {
        return 0;
    }

    auto&       logger = ::ll::mod::NativeMod::current()->getLogger();
    std::string gid    = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("addPermissionsToGroup: 组 '%s' 未找到。", groupName.c_str());
        return 0;
    }

    logger.debug("批量操作：向组 '%s' 添加 %zu 条权限规则...", groupName.c_str(), permissionRules.size());

    if (!db_->beginTransaction()) {
        logger.error("addPermissionsToGroup 无法开始事务。");
        return 0;
    }

    size_t      successCount = 0;
    std::string insertSql    = db_->getInsertOrIgnoreSql(
        "group_permissions",
        "group_id, permission_rule",
        "?, ?",
        "group_id, permission_rule"
    );

    for (const auto& rule : permissionRules) {
        if (rule.empty() || rule == "-") {
            logger.warn("addPermissionsToGroup: 提供的权限规则 '%s' 无效，跳过。", rule.c_str());
            continue;
        }
        if (db_->executePrepared(insertSql, {gid, rule})) {
            successCount++;
        }
    }

    if (db_->commit()) {
        if (successCount > 0) {
            enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
            logger.debug(
                "批量添加权限到组 '%s' 成功，提交了 %zu 条规则，缓存失效任务已入队。",
                groupName.c_str(),
                successCount
            );
        }
    } else {
        logger.error("addPermissionsToGroup 提交事务失败，正在回滚。");
        db_->rollback();
        return 0;
    }

    return successCount;
}

size_t PermissionManager::removePermissionsFromGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    if (permissionRules.empty()) {
        return 0;
    }

    auto&       logger = ::ll::mod::NativeMod::current()->getLogger();
    std::string gid    = getCachedGroupId(groupName);
    if (gid.empty()) {
        logger.warn("removePermissionsFromGroup: 组 '%s' 未找到。", groupName.c_str());
        return 0;
    }

    logger.debug("批量操作：从组 '%s' 移除 %zu 条权限规则...", groupName.c_str(), permissionRules.size());

    if (!db_->beginTransaction()) {
        logger.error("removePermissionsFromGroup 无法开始事务。");
        return 0;
    }

    size_t      successCount = 0;
    std::string deleteSql    = "DELETE FROM group_permissions WHERE group_id = ? AND permission_rule = ?;";

    for (const auto& rule : permissionRules) {
        if (rule.empty() || rule == "-") {
            continue;
        }
        if (db_->executePrepared(deleteSql, {gid, rule})) {
            successCount++;
        }
    }

    if (db_->commit()) {
        if (successCount > 0) {
            enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
            logger.debug(
                "批量移除权限从组 '%s' 成功，提交了 %zu 条规则，缓存失效任务已入队。",
                groupName.c_str(),
                successCount
            );
        }
    } else {
        logger.error("removePermissionsFromGroup 提交事务失败，正在回滚。");
        db_->rollback();
        return 0;
    }

    return successCount;
}

} // namespace permission
} // namespace BA
