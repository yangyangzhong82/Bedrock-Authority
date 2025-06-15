#pragma once

// Define BA_API based on whether we are building the DLL or using it
#ifdef BA_EXPORTS // This should be defined by the build system when building the DLL
    #ifdef _WIN32
        #define BA_API __declspec(dllexport)
    #else
        #define BA_API __attribute__((visibility("default"))) // For GCC/Clang on Linux/macOS
    #endif
#else // We are using the DLL
    #ifdef _WIN32
        #define BA_API __declspec(dllimport)
    #else
        #define BA_API // Import is default visibility on GCC/Clang
    #endif
#endif

#include <string>
#include <vector>
#include <unordered_map> 
#include <map>         
#include <shared_mutex>  
#include <set>           
#include <thread>             
#include <vector>             
#include <queue>              
#include <mutex>              
#include <condition_variable> 
#include <regex>            
#include <atomic>            

namespace BA { namespace db { class IDatabase; } } // 前向声明 IDatabase

namespace BA {
namespace permission {

// 异步缓存失效任务类型
enum class CacheInvalidationTaskType {
    GROUP_MODIFIED,         // 组权限或继承关系改变，需要失效该组及其子组的权限缓存，以及受影响玩家的权限缓存
    PLAYER_GROUP_CHANGED,   // 玩家组关系改变，需要失效该玩家的权限缓存
    ALL_GROUPS_MODIFIED,    // 所有组的权限或默认权限改变，需要失效所有组的权限缓存
    ALL_PLAYERS_MODIFIED,   // 所有玩家的默认权限改变，需要失效所有玩家的权限缓存
    SHUTDOWN                // 停止工作线程
};

// 异步缓存失效任务结构体
struct CacheInvalidationTask {
    CacheInvalidationTaskType type;
    std::string data; // 存储组名或玩家UUID等数据
};

// 结构体用于返回组的详细信息
struct BA_API GroupDetails {
    std::string id;
    std::string name;
    std::string description;
    int priority;
    bool isValid = false; // 指示此结构体是否包含有效数据

    // 默认构造函数
    GroupDetails() : id(""), name(""), description(""), priority(0), isValid(false) {}

    // 带参数的构造函数
    GroupDetails(std::string id, std::string name, std::string description, int priority)
        : id(std::move(id)), name(std::move(name)), description(std::move(description)), priority(priority), isValid(true) {}
};

// 结构体用于在 getAllPermissionsForPlayer 中统一处理组权限
struct BA_API GroupPermissionInfo {
    std::string id;
    std::string name;
    int priority;
    std::vector<std::string> directPermissionRules; // 该组直接拥有的权限规则

    // 构造函数
    GroupPermissionInfo(std::string id, std::string name, int priority, std::vector<std::string> rules)
        : id(std::move(id)), name(std::move(name)), priority(priority), directPermissionRules(std::move(rules)) {}
};

// 编译后的权限规则结构体
struct CompiledPermissionRule {
    std::string pattern; // 原始权限模式字符串，例如 "my.perm.*"
    std::regex  regex;   // 编译后的正则表达式对象
    bool        state;   // true 表示授予，false 表示否定

    // 构造函数
    CompiledPermissionRule(std::string p, std::regex r, bool s) : pattern(std::move(p)), regex(std::move(r)), state(s) {}
};


class BA_API PermissionManager { // Export the class itself if needed, or just members
public:
    static PermissionManager& getInstance(); // Static instance getter might not need export depending on usage

    /// 使用数据库实例初始化管理器（必须在调用其他 API 之前调用）。
    /// 如果初始化成功，返回 true；否则返回 false。
    /// @param db 数据库实例指针。
    /// @param enableWarmup 是否在启动时预热缓存。
    /// @param threadPoolSize 缓存失效工作线程池大小。
    bool init(db::IDatabase* db, bool enableWarmup = true, unsigned int threadPoolSize = 4);
    /// 关闭权限管理器，停止异步工作线程。
    void shutdown();

    /// 注册一个新权限。如果已存在或出错，则返回 false。
    bool registerPermission(const std::string& name, const std::string& description = "", bool defaultValue = false);
    /// 检查权限是否存在
    bool permissionExists(const std::string& name);
    /// 获取所有权限名称
    std::vector<std::string> getAllPermissions();

    /// 创建一个权限组。如果已存在或出错，则返回 false。
    bool createGroup(const std::string& groupName, const std::string& description = "");
    /// 检查组是否存在
    bool groupExists(const std::string& groupName);
    /// 获取所有组名称
    std::vector<std::string> getAllGroups();
    /// 删除一个权限组。如果不存在或出错，则返回 false。
    bool deleteGroup(const std::string& groupName);

    /// 将权限添加到组。如果组或权限不存在，或已分配，则返回 false。
    bool addPermissionToGroup(const std::string& groupName, const std::string& permissionName);
    /// 从组中移除权限
    bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionName);
    /// 获取组直接拥有的权限（不包括继承的）
    std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName);
    /// 获取组的最终权限（包括继承的）
    std::vector<CompiledPermissionRule> getPermissionsOfGroup(const std::string& groupName);

    /// 添加继承：子组继承父组。如果无效或已设置，则返回 false。
    bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /// 移除继承
    bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    /// 获取直接父组
    std::vector<std::string> getParentGroups(const std::string& groupName);

    /// 将玩家分配到权限组
    bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName);
    /// 从权限组中移除玩家
    bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName);
    /// 获取玩家所属的组名称
    std::vector<std::string> getPlayerGroups(const std::string& playerUuid);
    /// 获取玩家所属的组 ID (字符串形式)
    std::vector<std::string> getPlayerGroupIds(const std::string& playerUuid);
    /// 获取权限组中的玩家
    std::vector<std::string> getPlayersInGroup(const std::string& groupName);
    /// 获取玩家所属的所有组及其优先级。
    std::vector<GroupDetails> getPlayerGroupsWithPriorities(const std::string& playerUuid);
    /// 获取玩家最终生效的所有权限规则（考虑优先级、继承和否定），这些规则是未展开的原始规则。
    /// 实际的权限解析（通配符匹配）发生在 hasPermission 函数中。
    std::vector<CompiledPermissionRule> getAllPermissionsForPlayer(const std::string& playerUuid);

    /// 设置权限组的优先级（优先级越高越优先）
    bool setGroupPriority(const std::string& groupName, int priority);
    /// 获取权限组的优先级
    int getGroupPriority(const std::string& groupName);
    /// 检查玩家是否拥有特定权限（支持通配符和否定）
    bool hasPermission(const std::string& playerUuid, const std::string& permissionNode);

    /// 获取组的详细信息（名称、描述、优先级）。如果组不存在，返回 isValid 为 false 的 GroupDetails。
    GroupDetails getGroupDetails(const std::string& groupName);

    /// 更新组的描述。
    bool updateGroupDescription(const std::string& groupName, const std::string& newDescription);

    /// 获取组的描述。如果组不存在，返回空字符串。
    std::string getGroupDescription(const std::string& groupName);
    /// @brief 将玩家一次性添加到多个权限组。
    /// @param playerUuid 玩家的 UUID。
    /// @param groupNames 要添加到的组名列表。
    /// @return 成功添加的组的数量。如果玩家或任何组不存在，或发生数据库错误，则可能小于请求的数量。
    size_t addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);

    /// @brief 一次性从多个权限组中移除玩家。
    /// @param playerUuid 玩家的 UUID。
    /// @param groupNames 要从中移除的组名列表。
    /// @return 成功移除的组的数量。
     size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);

    /// @brief 将多个权限规则一次性添加到组。
    /// @param groupName 组的名称。
    /// @param permissionRules 要添加的权限规则列表。
    /// @return 成功添加的规则的数量。
     size_t addPermissionsToGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);

    /// @brief 一次性从组中移除多个权限规则。
    /// @param groupName 组的名称。
    /// @param permissionRules 要移除的权限规则列表。
    /// @return 成功移除的规则的数量。
     size_t
    removePermissionsFromGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);

private:
    PermissionManager() = default;
    // 禁用拷贝构造函数和拷贝赋值运算符，确保单例模式
    PermissionManager(const PermissionManager&) = delete;
    PermissionManager& operator=(const PermissionManager&) = delete;

    void ensureTables(); // 确保表存在

    // 辅助函数：根据名称从权限表或组表中获取 ID
    std::string getIdByName(const std::string& table, const std::string& name);
    // 辅助函数：从数据库获取组 ID (无锁版本)
    std::string _getGroupIdFromDb(const std::string& groupName);

    // 辅助函数：将通配符模式转换为正则表达式对象
    std::regex wildcardToRegex(const std::string& pattern);

    // 新增辅助函数：递归获取所有子组 (现在使用缓存)
    std::set<std::string> getChildGroupsRecursive(const std::string& groupName);
    // 新增辅助函数：获取受特定组修改影响的所有玩家 UUID (现在使用缓存)
    std::vector<std::string> getAffectedPlayersByGroup(const std::string& groupName);

    // --- 缓存相关 ---
    // 从缓存或数据库获取组 ID
    std::string getCachedGroupId(const std::string& groupName);
    // 使特定组的缓存失效
    void invalidateGroupCache(const std::string& groupName);
    // 更新或插入组到缓存
    void updateGroupCache(const std::string& groupName, const std::string& groupId);
    // 从数据库加载所有组到缓存（可选，可在 init 时调用）
    void populateGroupCache();
    // 使特定玩家的权限缓存失效
    void invalidatePlayerPermissionsCache(const std::string& playerUuid);
    // 使所有玩家的权限缓存失效
    void invalidateAllPlayerPermissionsCache();
    // 使特定组的权限缓存失效
    void invalidateGroupPermissionsCache(const std::string& groupName);
    // 使所有组的权限缓存失效
    void invalidateAllGroupPermissionsCache();
    // 从数据库加载所有组的权限到缓存
    void populateGroupPermissionsCache();
    // 新增：填充权限默认值缓存
    void populatePermissionDefaultsCache();

    // 新增：继承图缓存
    std::unordered_map<std::string, std::set<std::string>> parentToChildren_; // 父组名 -> 子组名集合
    std::unordered_map<std::string, std::set<std::string>> childToParents_;   // 子组名 -> 父组名集合
    mutable std::shared_mutex inheritanceCacheMutex_; // 用于保护继承图缓存的读写锁

    // 新增：填充继承图缓存
    void populateInheritanceCache();
    // 新增：更新继承图缓存
    void updateInheritanceCache(const std::string& groupName, const std::string& parentGroupName);
    // 新增：从继承图缓存中移除
    void removeInheritanceFromCache(const std::string& groupName, const std::string& parentGroupName);

    // 辅助函数：检查继承图中是否存在从 startNode 到 endNode 的路径 (用于循环检测)
    bool hasPath(const std::string& startNode, const std::string& endNode);

    // 辅助函数：递归获取一个组的所有祖先组（包括自身）
    std::set<std::string> getAllAncestorGroups(const std::string& groupName);

    // --- 结束缓存相关 ---

    db::IDatabase* db_ = nullptr;

    // 组名到组 ID 的缓存
    std::unordered_map<std::string, std::string> groupNameCache_;
    // 用于保护组名缓存访问的读写锁
    mutable std::shared_mutex cacheMutex_; // mutable 允许在 const 方法中锁定

    // 玩家权限缓存
    std::unordered_map<std::string, std::vector<CompiledPermissionRule>> playerPermissionsCache_;
    // 用于保护玩家权限缓存的读写锁
    mutable std::shared_mutex playerPermissionsCacheMutex_;

    // 玩家组缓存
    std::unordered_map<std::string, std::vector<GroupDetails>> playerGroupsCache_;
    // 用于保护玩家组缓存的读写锁
    mutable std::shared_mutex playerGroupsCacheMutex_;

    // 组权限缓存 (新添加)
    std::unordered_map<std::string, std::vector<CompiledPermissionRule>> groupPermissionsCache_;
    // 用于保护组权限缓存的读写锁 (新添加)
    mutable std::shared_mutex groupPermissionsCacheMutex_;

    // 新增：权限默认值缓存
    std::unordered_map<std::string, bool> permissionDefaultsCache_;
    // 用于保护权限默认值缓存的读写锁
    mutable std::shared_mutex permissionDefaultsCacheMutex_;

    // --- 异步任务队列相关 ---
    std::queue<CacheInvalidationTask> taskQueue_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::vector<std::thread> workerThreads_; // 线程池
    std::atomic<bool> running_ = false;      // 控制工作线程的运行状态
    unsigned int threadPoolSize_ = 4;        // 线程池大小

    // 新增：用于任务合并的成员
    std::set<std::string> pendingGroupModifiedTasks_; // 存储待处理的 GROUP_MODIFIED 任务的组名
    bool allGroupsModifiedPending_ = false;           // 标记 ALL_GROUPS_MODIFIED 任务是否已在队列中
    std::mutex pendingTasksMutex_;                    // 保护 pendingGroupModifiedTasks_ 和 allGroupsModifiedPending_

    // 将任务推入队列
    void enqueueTask(CacheInvalidationTask task);
    // 工作线程处理任务的函数
    void processTasks();

    // 实际执行缓存失效的内部函数 (由工作线程调用)
    void _invalidateGroupPermissionsCache(const std::string& groupName);
    void _invalidatePlayerPermissionsCache(const std::string& playerUuid);
    void _invalidatePlayerGroupsCache(const std::string& playerUuid); 
    void _invalidateAllGroupPermissionsCache();
    void _invalidateAllPlayerPermissionsCache();
    // --- 结束异步任务队列相关 ---
};

} // namespace permission
} // namespace BA
