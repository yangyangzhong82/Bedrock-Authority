#include "permission/PermissionManagerImpl.h" // 包含实现类的头文件
#include "db/IDatabase.h"                     // 包含数据库接口
#include "ll/api/io/Logger.h"                 // 包含日志库
#include "ll/api/mod/NativeMod.h"             // 包含原生模块接口
#include "permission/AsyncCacheInvalidator.h" // 包含异步缓存失效器
#include "permission/PermissionCache.h"       // 包含权限缓存
#include "permission/PermissionStorage.h"     // 包含权限存储
#include "permission/events/PlayerJoinGroupEvent.h" // 包含玩家加入组事件
#include "permission/events/PlayerLeaveGroupEvent.h" // 包含玩家离开组事件
#include "permission/events/GroupPermissionChangeEvent.h" // 包含组权限变更事件
#include <algorithm>                          // 包含算法库
#include <functional>                         // 包含函数对象库
#include <ll/api/event/EventBus.h>            // 包含事件总线



namespace BA {
namespace permission {

using namespace std; // 将 using namespace std; 移动到全局作用域

// --- 辅助函数：通配符转正则表达式（从原始类中移出） ---
/**
 * @brief 将通配符模式转换为正则表达式。
 * @param pattern 包含通配符的字符串模式。
 * @return 对应的正则表达式对象。
 */
std::regex PermissionManager::PermissionManagerImpl::wildcardToRegex(const std::string& pattern) {
    std::string regexPatternStr = "^"; // 正则表达式以字符串开头
    for (char c : pattern) {
        if (c == '*') {
            regexPatternStr += ".*"; // 将 '*' 转换为 '.*' (匹配任意字符零次或多次)
        } else if (std::string(".\\+?^$[](){}|").find(c) != std::string::npos) {
            regexPatternStr += '\\'; // 转义正则表达式特殊字符
            regexPatternStr += c;
        } else {
            regexPatternStr += c; // 其他字符直接添加
        }
    }
    regexPatternStr += "$"; // 正则表达式以字符串结尾
    // 使用 ECMAScript 语法和不区分大小写的匹配
    return std::regex(regexPatternStr, std::regex_constants::ECMAScript | std::regex_constants::icase);
}

// --- 实现类生命周期 ---

/**
 * @brief PermissionManagerImpl 构造函数。
 */
PermissionManager::PermissionManagerImpl::PermissionManagerImpl() = default;

/**
 * @brief PermissionManagerImpl 析构函数。
 *        确保在对象销毁时调用 shutdown，以防用户忘记调用。
 */
PermissionManager::PermissionManagerImpl::~PermissionManagerImpl() {
    if (m_initialized) {
        shutdown();
    }
}

/**
 * @brief 初始化权限管理器实现。
 * @param db 数据库接口指针。
 * @param enableWarmup 是否启用预热缓存。
 * @param threadPoolSize 线程池大小。
 * @return 如果初始化成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::init(db::IDatabase* db, bool enableWarmup, unsigned int threadPoolSize) {
    if (m_initialized) {
        ::ll::mod::NativeMod::current()->getLogger().warn("权限管理器已初始化，无需重复初始化。");
        return true;
    }
    if (!db) {
        ::ll::mod::NativeMod::current()->getLogger().error("权限管理器初始化失败：数据库指针为空。");
        return false;
    }

    try {
        // 创建权限存储、缓存和异步缓存失效器实例
        m_storage     = make_unique<internal::PermissionStorage>(db);
        m_cache       = make_unique<internal::PermissionCache>();
        m_invalidator = make_unique<internal::AsyncCacheInvalidator>(*m_cache, *m_storage);

        // 确保数据库表存在
        m_storage->ensureTables();

        // 如果启用预热，则填充所有缓存
        if (enableWarmup) {
            populateAllCaches();
        }

        // 启动异步缓存失效器
        m_invalidator->start(threadPoolSize);
        m_initialized = true;
        ::ll::mod::NativeMod::current()->getLogger().info("权限管理器初始化成功。");
        return true;
    } catch (const std::exception& e) {
        // 捕获初始化过程中可能发生的异常
        ::ll::mod::NativeMod::current()->getLogger().error("权限管理器初始化失败：{}", e.what());
        return false;
    }
}

/**
 * @brief 关闭权限管理器实现。
 *        停止异步缓存失效器并重置初始化状态。
 */
void PermissionManager::PermissionManagerImpl::shutdown() {
    if (!m_initialized) {
        // 如果未初始化，则直接返回
        return;
    }
    // 停止异步缓存失效器
    m_invalidator->stop();
    m_initialized = false;
    ::ll::mod::NativeMod::current()->getLogger().info("权限管理器已关闭。");
}

/**
 * @brief 预热所有权限缓存。
 *        此方法会从存储层加载数据并填充到缓存中，以提高后续查询性能。
 */
void PermissionManager::PermissionManagerImpl::populateAllCaches() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.info("正在预热权限缓存...");

    // 填充组名 -> ID 缓存
    std::vector<std::string> groupNamesVec = m_storage->fetchAllGroupNames();
    std::set<std::string> groupNamesSet(groupNamesVec.begin(), groupNamesVec.end()); // 将向量转换为集合
    
    // 批量获取所有组名及其 ID
    std::unordered_map<std::string, std::string> groupNameMap = m_storage->fetchGroupIdsByNames(groupNamesSet);
    m_cache->populateAllGroups(std::move(groupNameMap));
    logger.debug("已使用 {} 个条目填充组名缓存。", groupNameMap.size()); // 使用 map 大小确保准确性

    // 填充继承缓存
    auto                               parentToChildren = m_storage->fetchAllInheritance();
    unordered_map<string, set<string>> childToParents;
    for (const auto& pair : parentToChildren) {
        const string& parent = pair.first;
        for (const string& child : pair.second) {
            childToParents[child].insert(parent);
        }
    }
    m_cache->populateInheritance(std::move(parentToChildren), std::move(childToParents));
    logger.debug("已填充继承缓存。");

    // 填充权限默认值缓存
    m_cache->populateAllPermissionDefaults(m_storage->fetchAllPermissionDefaults());
    logger.debug("已填充权限默认值缓存。");

    // 组权限缓存由 getPermissionsOfGroup 按需填充
    // 玩家权限和组缓存也是按需填充

    // 预热组权限缓存
    for (const auto& pair : m_cache->getAllGroups()) {
        const std::string& groupName = pair.first;
        // 调用 getPermissionsOfGroup 会自动填充 m_groupPermissionsCache
        getPermissionsOfGroup(groupName);
        logger.debug("已预热组 '{}' 的权限缓存。", groupName);
    }

    logger.info("权限缓存预热完成。");
}

// --- 缓存感知辅助函数 ---
/**
 * @brief 获取缓存的组 ID，如果缓存中不存在则从存储层获取并缓存。
 * @param groupName 组名称。
 * @return 组的 ID 字符串。
 */
std::string PermissionManager::PermissionManagerImpl::getCachedGroupId(const std::string& groupName) {
    // 1. 检查缓存中是否存在组ID
    auto cachedId = m_cache->findGroupId(groupName);
    if (cachedId) {
        return *cachedId; // 缓存命中，直接返回
    }

    // 2. 缓存未命中，从存储层查询组ID
    std::string idFromDb = m_storage->fetchGroupIdByName(groupName);

    // 3. 如果在存储层找到，则将其存储到缓存中
    if (!idFromDb.empty()) {
        m_cache->storeGroup(groupName, idFromDb);
    }
    return idFromDb;
}

// --- 公共 API 实现 ---

/**
 * @brief 注册一个权限。
 * @param name 权限名称。
 * @param description 权限描述。
 * @param defaultValue 权限的默认值。
 * @return 如果注册成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::registerPermission(
    const std::string& name,
    const std::string& description,
    bool               defaultValue
) {
    if (m_storage->upsertPermission(name, description, defaultValue)) {
        m_cache->storePermissionDefault(name, defaultValue);
        // 权限注册或更新后，需要使所有组和玩家的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::ALL_GROUPS_MODIFIED, ""});
        m_invalidator->enqueueTask({CacheInvalidationTaskType::ALL_PLAYERS_MODIFIED, ""});
        return true;
    }
    return false;
}

/**
 * @brief 检查权限是否存在。
 * @param name 权限名称。
 * @return 如果权限存在则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::permissionExists(const std::string& name) {
    return m_storage->permissionExists(name);
}

/**
 * @brief 获取所有已注册的权限名称。
 * @return 包含所有权限名称的向量。
 */
std::vector<std::string> PermissionManager::PermissionManagerImpl::getAllPermissions() {
    return m_storage->fetchAllPermissionNames();
}

/**
 * @brief 创建一个新组。
 * @param groupName 组名称。
 * @param description 组描述。
 * @return 如果创建成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::createGroup(
    const std::string& groupName,
    const std::string& description
) {
    string groupId;
    if (m_storage->createGroup(groupName, description, groupId) && !groupId.empty()) {
        m_cache->storeGroup(groupName, groupId);
        // 新组创建时，无需使缓存失效，因为它还没有权限或玩家。
        return true;
    }
    return false;
}

/**
 * @brief 删除一个组。
 * @param groupName 要删除的组名称。
 * @return 如果删除成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::deleteGroup(const std::string& groupName) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) {
        return false; // 组不存在
    }

    // 使与此组相关的所有继承缓存失效
    auto parents  = m_cache->getAllAncestorGroups(groupName);
    auto children = m_cache->getChildGroupsRecursive(groupName);

    if (m_storage->deleteGroup(groupId)) {
        // 从数据库删除后使缓存失效
        m_cache->invalidateGroup(groupName);
        // 将所有相关组及其玩家的缓存失效任务加入队列
        for (const auto& child : children)
            m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, child});
        return true;
    }
    return false;
}

/**
 * @brief 检查组是否存在。
 * @param groupName 组名称。
 * @return 如果组存在则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::groupExists(const std::string& groupName) {
    return !getCachedGroupId(groupName).empty();
}

/**
 * @brief 获取所有组的名称。
 * @return 包含所有组名称的向量。
 */
std::vector<std::string> PermissionManager::PermissionManagerImpl::getAllGroups() {
    return m_storage->fetchAllGroupNames();
}

/**
 * @brief 获取组的详细信息。
 * @param groupName 组名称。
 * @return 组的详细信息对象。
 */
GroupDetails PermissionManager::PermissionManagerImpl::getGroupDetails(const std::string& groupName) {
    return m_storage->fetchGroupDetails(groupName);
}

/**
 * @brief 更新组的描述。
 * @param groupName 组名称。
 * @param newDescription 新的组描述。
 * @return 如果更新成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::updateGroupDescription(
    const std::string& groupName,
    const std::string& newDescription
) {
    return m_storage->updateGroupDescription(groupName, newDescription);
}

/**
 * @brief 获取组的描述。
 * @param groupName 组名称。
 * @return 组的描述字符串。
 */
std::string PermissionManager::PermissionManagerImpl::getGroupDescription(const std::string& groupName) {
    return m_storage->fetchGroupDescription(groupName);
}

/**
 * @brief 向组添加权限规则。
 * @param groupName 组名称。
 * @param permissionRule 权限规则字符串。
 * @return 如果添加成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::addPermissionToGroup(
    const std::string& groupName,
    const std::string& permissionRule
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false; // 组不存在

    std::string groupNameForEvent = groupName;
    std::string permissionRuleForEvent = permissionRule;
    bool isAddForEvent = true;

    auto beforeEvent = event::GroupPermissionChangeBeforeEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent);
    ll::event::EventBus::getInstance().publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        ::ll::mod::NativeMod::current()->getLogger().debug(
            "组 '{}' 添加权限 '{}' 的事件被取消。",
            groupName,
            permissionRule
        );
        return false;
    }

    if (m_storage->addPermissionToGroup(groupId, permissionRule)) {
        // 组权限修改后，需要使该组的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        
        ll::event::EventBus::getInstance().publish(event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent));
        return true;
    }
    return false;
}

/**
 * @brief 从组中移除权限规则。
 * @param groupName 组名称。
 * @param permissionRule 权限规则字符串。
 * @return 如果移除成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::removePermissionFromGroup(
    const std::string& groupName,
    const std::string& permissionRule
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false; // 组不存在

    std::string groupNameForEvent = groupName;
    std::string permissionRuleForEvent = permissionRule;
    bool isAddForEvent = false;

    auto beforeEvent = event::GroupPermissionChangeBeforeEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent);
    ll::event::EventBus::getInstance().publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        ::ll::mod::NativeMod::current()->getLogger().debug(
            "组 '{}' 移除权限 '{}' 的事件被取消。",
            groupName,
            permissionRule
        );
        return false;
    }

    if (m_storage->removePermissionFromGroup(groupId, permissionRule)) {
        // 组权限修改后，需要使该组的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        
        ll::event::EventBus::getInstance().publish(event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent));
        return true;
    }
    return false;
}

/**
 * @brief 获取组的直接权限。
 * @param groupName 组名称。
 * @return 包含组直接权限规则的向量。
 */
std::vector<std::string>
PermissionManager::PermissionManagerImpl::getDirectPermissionsOfGroup(const std::string& groupName) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return {}; // 组不存在
    return m_storage->fetchDirectPermissionsOfGroup(groupId);
}

/**
 * @brief 获取组的所有有效权限（包括继承的权限）。
 * @param groupName 组名称。
 * @return 包含编译后的权限规则的向量。
 */
std::vector<CompiledPermissionRule>
PermissionManager::PermissionManagerImpl::getPermissionsOfGroup(const std::string& groupName) {
    // 1. 检查缓存
    auto cachedPerms = m_cache->findGroupPermissions(groupName);
    if (cachedPerms) {
        return *cachedPerms; // 缓存命中，直接返回
    }

    // 2. 缓存未命中，计算权限
    // 获取所有祖先组的名称
    set<string> ancestorNames = m_cache->getAllAncestorGroups(groupName);
    // 批量获取所有相关组的详情
    unordered_map<string, GroupDetails> relevantGroupsMap = m_storage->fetchGroupDetailsByNames(ancestorNames);
    vector<GroupDetails>                relevantGroups;
    for (const auto& name : ancestorNames) {
        auto it = relevantGroupsMap.find(name);
        if (it != relevantGroupsMap.end()) {
            relevantGroups.push_back(it->second);
        }
    }

    // 根据优先级对相关组进行排序
    sort(relevantGroups.begin(), relevantGroups.end(), [](const auto& a, const auto& b) {
        return a.priority < b.priority;
    });

    // 计算最终的有效权限状态
    map<string, bool> effectiveState;
    for (const auto& group : relevantGroups) {
        auto rules = m_storage->fetchDirectPermissionsOfGroup(group.id);
        for (const auto& rule : rules) {
            bool   isNegated = rule.starts_with('-'); // 检查是否是负向权限
            string baseName  = isNegated ? rule.substr(1) : rule; // 获取权限基础名称
            if (baseName.empty()) continue;
            effectiveState[baseName] = !isNegated; // 设置权限的有效状态
        }
    }

    // 将有效权限转换为编译后的权限规则
    vector<CompiledPermissionRule> finalPerms;
    for (const auto& [pattern, state] : effectiveState) {
        try {
            finalPerms.emplace_back(pattern, wildcardToRegex(pattern), state);
        } catch (const std::regex_error& e) {
            ::ll::mod::NativeMod::current()->getLogger().error("权限模式 '{}' 的正则表达式错误：{}", pattern, e.what());
        }
    }

    // 根据模式长度对权限进行排序（长模式优先）
    sort(finalPerms.begin(), finalPerms.end(), [](const auto& a, const auto& b) {
        return a.pattern.length() > b.pattern.length();
    });

    // 3. 存储到缓存并返回
    m_cache->storeGroupPermissions(groupName, finalPerms);
    return finalPerms;
}

/**
 * @brief 批量向组添加权限规则。
 * @param groupName 组名称。
 * @param permissionRules 权限规则字符串向量。
 * @return 成功添加的权限规则数量。
 */
size_t PermissionManager::PermissionManagerImpl::addPermissionsToGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return 0; // 组不存在

    size_t successCount = 0;
    for (const auto& rule : permissionRules) {
        std::string groupNameForEvent = groupName;
        std::string permissionRuleForEvent = rule;
        bool isAddForEvent = true;

        auto beforeEvent = event::GroupPermissionChangeBeforeEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent);
        ll::event::EventBus::getInstance().publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            ::ll::mod::NativeMod::current()->getLogger().debug(
                "组 '{}' 批量添加权限 '{}' 的事件被取消。",
                groupName,
                rule
            );
            continue; // 跳过此规则，继续处理下一个
        }

        if (m_storage->addPermissionToGroup(groupId, rule)) {
            successCount++;
            ll::event::EventBus::getInstance().publish(event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent));
        }
    }

    if (successCount > 0) {
        // 组权限修改后，需要使该组的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
    }
    return successCount;
}

/**
 * @brief 批量从组中移除权限规则。
 * @param groupName 组名称。
 * @param permissionRules 权限规则字符串向量。
 * @return 成功移除的权限规则数量。
 */
size_t PermissionManager::PermissionManagerImpl::removePermissionsFromGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return 0; // 组不存在

    size_t successCount = 0;
    for (const auto& rule : permissionRules) {
        std::string groupNameForEvent = groupName;
        std::string permissionRuleForEvent = rule;
        bool isAddForEvent = false;

        auto beforeEvent = event::GroupPermissionChangeBeforeEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent);
        ll::event::EventBus::getInstance().publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            ::ll::mod::NativeMod::current()->getLogger().debug(
                "组 '{}' 批量移除权限 '{}' 的事件被取消。",
                groupName,
                rule
            );
            continue; // 跳过此规则，继续处理下一个
        }

        if (m_storage->removePermissionFromGroup(groupId, rule)) {
            successCount++;
            ll::event::EventBus::getInstance().publish(event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent));
        }
    }

    if (successCount > 0) {
        // 组权限修改后，需要使该组的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
    }
    return successCount;
}

/**
 * @brief 添加组继承关系。
 * @param groupName 子组名称。
 * @param parentGroupName 父组名称。
 * @return 如果添加成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::addGroupInheritance(
    const std::string& groupName,
    const std::string& parentGroupName
) {
    // 检查是否尝试继承自身或形成循环
    if (groupName == parentGroupName || m_cache->hasPath(parentGroupName, groupName)) {
        return false; // 检测到循环或无效继承
    }
    string groupId       = getCachedGroupId(groupName);
    string parentGroupId = getCachedGroupId(parentGroupName);
    if (groupId.empty() || parentGroupId.empty()) return false; // 组或父组不存在

    if (m_storage->addGroupInheritance(groupId, parentGroupId)) {
        m_cache->addInheritance(groupName, parentGroupName);
        // 组继承关系修改后，需要使子组的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

/**
 * @brief 移除组继承关系。
 * @param groupName 子组名称。
 * @param parentGroupName 父组名称。
 * @return 如果移除成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::removeGroupInheritance(
    const std::string& groupName,
    const std::string& parentGroupName
) {
    string groupId       = getCachedGroupId(groupName);
    string parentGroupId = getCachedGroupId(parentGroupName);
    if (groupId.empty() || parentGroupId.empty()) return false; // 组或父组不存在

    if (m_storage->removeGroupInheritance(groupId, parentGroupId)) {
        m_cache->removeInheritance(groupName, parentGroupName);
        // 组继承关系修改后，需要使子组的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

/**
 * @brief 获取组的所有祖先组（包括自身）。
 * @param groupName 组名称。
 * @return 包含所有祖先组名称的向量。
 */
std::vector<std::string> PermissionManager::PermissionManagerImpl::getAllAncestorGroups(const std::string& groupName) {
    auto ancestors = m_cache->getAllAncestorGroups(groupName);
    ancestors.erase(groupName); // 移除自身
    return std::vector<std::string>(ancestors.begin(), ancestors.end());
}

/**
 * @brief 获取组的直接父组。
 * @param groupName 组名称。
 * @return 包含直接父组名称的向量。
 */
std::vector<std::string> PermissionManager::PermissionManagerImpl::getDirectParentGroups(const std::string& groupName) {
    std::vector<std::string> directParents;
    std::string              groupId = getCachedGroupId(groupName);
    if (groupId.empty()) {
        return directParents; // 组不存在
    }
    // 从存储层获取直接父组的ID
    std::vector<std::string> parentGroupIds = m_storage->fetchDirectParentGroupIds(groupId);
    // 批量将ID转换为组名
    std::unordered_map<std::string, std::string> idToNameMap = m_storage->fetchGroupNamesByIds(parentGroupIds);
    for (const auto& parentId : parentGroupIds) {
        auto it = idToNameMap.find(parentId);
        if (it != idToNameMap.end()) {
            directParents.push_back(it->second);
        }
    }
    return directParents;
}

/**
 * @brief 设置组的优先级。
 * @param groupName 组名称。
 * @param priority 新的优先级值。
 * @return 如果设置成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::setGroupPriority(const std::string& groupName, int priority) {
    if (m_storage->updateGroupPriority(groupName, priority)) {
        // 组优先级修改后，需要使该组的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

/**
 * @brief 获取组的优先级。
 * @param groupName 组名称。
 * @return 组的优先级值。
 */
int PermissionManager::PermissionManagerImpl::getGroupPriority(const std::string& groupName) {
    return m_storage->fetchGroupPriority(groupName);
}

/**
 * @brief 将玩家添加到组。
 * @param playerUuid 玩家的 UUID。
 * @param groupName 组名称。
 * @return 如果添加成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::addPlayerToGroup(
    const std::string& playerUuid,
    const std::string& groupName
) {
    return addPlayerToGroup(playerUuid, groupName, 0);
}
bool PermissionManager::PermissionManagerImpl::addPlayerToGroup(
    const std::string& playerUuid,
    const std::string& groupName,
    long long          durationSeconds
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false; // 组不存在

    std::optional<long long> expiryTimestamp;
    if (durationSeconds > 0) {
        long long currentTime =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        expiryTimestamp = currentTime + durationSeconds;
    }

    // 获取组名用于事件
    std::string actualGroupName = m_cache->findGroupName(groupId).value_or(groupName); // 尝试从缓存获取，否则使用传入的名称

    // 触发 PlayerJoinGroupBeforeEvent
    // 为了将参数传递给事件构造函数的引用，需要确保它们是可修改的左值。
    // 对于 const& 参数，可以直接传递临时对象或右值。
    // 但对于非 const& 参数，必须是可修改的左值。
    // 鉴于事件系统通常会复制事件对象，事件内部的引用通常指向事件对象自身的成员。
    // 这里为了修复编译错误，我们将传入的参数复制到局部变量，然后传递这些局部变量的引用。
    // 这样可以满足构造函数对非 const 左值引用的要求。
    std::string playerUuidForEvent = playerUuid;
    std::string groupNameForEvent = actualGroupName;
    std::optional<long long> expiryTimestampForEvent = expiryTimestamp;

    auto beforeEvent = event::PlayerJoinGroupBeforeEvent(playerUuidForEvent, groupNameForEvent, expiryTimestampForEvent);
    ll::event::EventBus::getInstance().publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        ::ll::mod::NativeMod::current()->getLogger().debug(
            "玩家 '{}' 加入组 '{}' 的事件被取消。",
            playerUuid,
            actualGroupName
        );
        return false;
    }

    if (m_storage->addPlayerToGroup(playerUuid, groupId, expiryTimestamp)) {
        // 玩家组关系修改后，需要使该玩家的权限和组缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
        
        // 触发 PlayerJoinGroupAfterEvent
        ll::event::EventBus::getInstance().publish(event::PlayerJoinGroupAfterEvent(playerUuidForEvent, groupNameForEvent, expiryTimestampForEvent));
        return true;
    }
    return false;
}
/**
 * @brief 将玩家从组中移除。
 * @param playerUuid 玩家的 UUID。
 * @param groupName 组名称。
 * @return 如果移除成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::removePlayerFromGroup(
    const std::string& playerUuid,
    const std::string& groupName
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false; // 组不存在

    std::string playerUuidForEvent = playerUuid;
    std::string groupNameForEvent = groupName;

    auto beforeEvent = event::PlayerLeaveGroupBeforeEvent(playerUuidForEvent, groupNameForEvent);
    ll::event::EventBus::getInstance().publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        ::ll::mod::NativeMod::current()->getLogger().debug(
            "玩家 '{}' 离开组 '{}' 的事件被取消。",
            playerUuid,
            groupName
        );
        return false;
    }

    if (m_storage->removePlayerFromGroup(playerUuid, groupId)) {
        // 玩家组关系修改后，需要使该玩家的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
        
        ll::event::EventBus::getInstance().publish(event::PlayerLeaveGroupAfterEvent(playerUuidForEvent, groupNameForEvent));
        return true;
    }
    return false;
}

/**
 * @brief 获取玩家所属的组及其优先级。
 * @param playerUuid 玩家的 UUID。
 * @return 包含玩家所属组详细信息（包括优先级）的向量。
 */
std::vector<GroupDetails>
PermissionManager::PermissionManagerImpl::getPlayerGroupsWithPriorities(const std::string& playerUuid) {
    // 1. 检查缓存
    auto cachedGroups = m_cache->findPlayerGroups(playerUuid);
    if (cachedGroups) {
        return *cachedGroups; // 缓存命中，直接返回
    }
    // 2. 缓存未命中，从数据库获取并存储到缓存
    auto groupsFromDb = m_storage->fetchPlayerGroupsWithDetails(playerUuid);
    m_cache->storePlayerGroups(playerUuid, groupsFromDb);
    return groupsFromDb;
}

/**
 * @brief 获取玩家所属的所有组名称。
 * @param playerUuid 玩家的 UUID。
 * @return 包含玩家所属组名称的向量。
 */
std::vector<std::string> PermissionManager::PermissionManagerImpl::getPlayerGroups(const std::string& playerUuid) {
    auto           details = getPlayerGroupsWithPriorities(playerUuid);
    vector<string> names;
    transform(details.begin(), details.end(), back_inserter(names), [](const auto& d) { return d.name; });
    return names;
}

/**
 * @brief 获取玩家所属的所有组 ID。
 * @param playerUuid 玩家的 UUID。
 * @return 包含玩家所属组 ID 的向量。
 */
std::vector<std::string> PermissionManager::PermissionManagerImpl::getPlayerGroupIds(const std::string& playerUuid) {
    auto           details = getPlayerGroupsWithPriorities(playerUuid);
    vector<string> ids;
    transform(details.begin(), details.end(), back_inserter(ids), [](const auto& d) { return d.id; });
    return ids;
}

/**
 * @brief 获取组中的所有玩家 UUID。
 * @param groupName 组名称。
 * @return 包含组中所有玩家 UUID 的向量。
 */
std::vector<std::string> PermissionManager::PermissionManagerImpl::getPlayersInGroup(const std::string& groupName) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return {}; // 组不存在
    return m_storage->fetchPlayersInGroup(groupId);
}

/**
 * @brief 获取玩家的所有有效权限（包括组权限和默认权限）。
 * @param playerUuid 玩家的 UUID。
 * @return 包含编译后的权限规则的向量。
 */
std::vector<CompiledPermissionRule>
PermissionManager::PermissionManagerImpl::getAllPermissionsForPlayer(const std::string& playerUuid) {
    // 1. 检查缓存
    auto cachedPerms = m_cache->findPlayerPermissions(playerUuid);
    if (cachedPerms) {
        return *cachedPerms; // 缓存命中，直接返回
    }

    // 2. 缓存未命中，计算权限
    map<string, bool> effectiveState;
    // 2.1. 添加默认权限
    // 尝试从缓存获取默认权限，如果未命中则从存储层获取并填充缓存
    auto defaults = m_cache->getAllPermissionDefaults(); // 使用公共方法获取缓存
    if (defaults.empty()) { // 如果缓存为空，则从存储层加载
        m_cache->populateAllPermissionDefaults(m_storage->fetchAllPermissionDefaults());
        defaults = m_cache->getAllPermissionDefaults(); // 重新获取填充后的缓存
    }
    for (const auto& [name, state] : defaults) {
        if (state) {
            effectiveState[name] = true;
        }
    }

    // 2.2. 获取所有相关组（直接所属和继承）及其权限
    auto        playerGroups = getPlayerGroupsWithPriorities(playerUuid);
    set<string> allRelevantGroupNames;
    for (const auto& group : playerGroups) {
        auto ancestors = m_cache->getAllAncestorGroups(group.name);
        allRelevantGroupNames.insert(ancestors.begin(), ancestors.end());
    }

    // 批量获取所有相关组的详情
    unordered_map<string, GroupDetails> allRelevantGroupsMap = m_storage->fetchGroupDetailsByNames(allRelevantGroupNames);
    vector<GroupDetails>                allRelevantGroupsDetails;
    for (const auto& name : allRelevantGroupNames) {
        auto it = allRelevantGroupsMap.find(name);
        if (it != allRelevantGroupsMap.end()) {
            allRelevantGroupsDetails.push_back(it->second);
        }
    }

    // 根据优先级对所有相关组进行排序
    sort(allRelevantGroupsDetails.begin(), allRelevantGroupsDetails.end(), [](const auto& a, const auto& b) {
        return a.priority < b.priority;
    });

    // 应用组权限
    for (const auto& group : allRelevantGroupsDetails) {
        auto rules = m_storage->fetchDirectPermissionsOfGroup(group.id);
        for (const auto& rule : rules) {
            bool   isNegated = rule.starts_with('-');
            string baseName  = isNegated ? rule.substr(1) : rule;
            if (baseName.empty()) continue;
            effectiveState[baseName] = !isNegated;
        }
    }

    // 将有效权限转换为编译后的权限规则
    vector<CompiledPermissionRule> finalPerms;
    for (const auto& [pattern, state] : effectiveState) {
        try {
            finalPerms.emplace_back(pattern, wildcardToRegex(pattern), state);
        } catch (const std::regex_error& e) {
            ::ll::mod::NativeMod::current()->getLogger().error("权限模式 '{}' 的正则表达式错误：{}", pattern, e.what());
        }
    }

    // 根据模式长度对权限进行排序（长模式优先）
    sort(finalPerms.begin(), finalPerms.end(), [](const auto& a, const auto& b) {
        return a.pattern.length() > b.pattern.length();
    });

    // 3. 存储到缓存并返回
    m_cache->storePlayerPermissions(playerUuid, finalPerms);
    return finalPerms;
}

/**
 * @brief 检查玩家是否拥有某个权限。
 * @param playerUuid 玩家的 UUID。
 * @param permissionNode 要检查的权限节点。
 * @return 如果玩家拥有该权限则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::hasPermission(
    const std::string& playerUuid,
    const std::string& permissionNode
) {
    auto rules = getAllPermissionsForPlayer(playerUuid);
    for (const auto& rule : rules) {
        if (regex_match(permissionNode, rule.regex)) {
            return rule.state; // 匹配到规则，返回其状态
        }
    }

    // 如果没有匹配到特定规则，则回退到权限的默认值
    auto defaultValue = m_cache->findPermissionDefault(permissionNode);
    if (defaultValue) {
        return *defaultValue; // 缓存中找到默认值
    }

    // 如果缓存中没有，则双重检查数据库（这种情况应该很少发生）
    auto defaults = m_storage->fetchAllPermissionDefaults();
    auto it       = defaults.find(permissionNode);
    if (it != defaults.end()) {
        m_cache->storePermissionDefault(permissionNode, it->second); // 存储到缓存
        return it->second;
    }

    return false; // 如果权限未注册，则默认拒绝
}

/**
 * @brief 批量将玩家添加到多个组。
 * @param playerUuid 玩家的 UUID。
 * @param groupNames 组名称向量。
 * @return 成功添加的组数量。
 */
size_t PermissionManager::PermissionManagerImpl::addPlayerToGroups(
    const std::string&              playerUuid,
    const std::vector<std::string>& groupNames
) {
    if (groupNames.empty()) return 0;
    vector<pair<string, string>> groupInfos;
    for (const auto& name : groupNames) {
        string id = getCachedGroupId(name);
        if (!id.empty()) {
            groupInfos.emplace_back(name, id);
        }
    }
    size_t count = m_storage->addPlayerToGroups(playerUuid, groupInfos);
    if (count > 0) {
        // 玩家组关系修改后，需要使该玩家的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
    }
    return count;
}

/**
 * @brief 批量将玩家从多个组中移除。
 * @param playerUuid 玩家的 UUID。
 * @param groupNames 组名称向量。
 * @return 成功移除的组数量。
 */
size_t PermissionManager::PermissionManagerImpl::removePlayerFromGroups(
    const std::string&              playerUuid,
    const std::vector<std::string>& groupNames
) {
    if (groupNames.empty()) return 0;
    vector<string> groupIds;
    for (const auto& name : groupNames) {
        string id = getCachedGroupId(name);
        if (!id.empty()) {
            groupIds.push_back(id);
        }
    }
    size_t count = m_storage->removePlayerFromGroups(playerUuid, groupIds);
    if (count > 0) {
        // 玩家组关系修改后，需要使该玩家的权限缓存失效
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
    }
    return count;
}

// 新增清理方法
void PermissionManager::PermissionManagerImpl::runPeriodicCleanup() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.debug("正在运行定期的权限清理任务...");
    std::vector<std::string> affectedPlayers = m_storage->deleteExpiredPlayerGroups();
    if (!affectedPlayers.empty()) {
        logger.debug("权限清理任务完成。已删除 {} 条过期记录，并使受影响玩家的缓存失效。", affectedPlayers.size());
        for (const auto& playerUuid : affectedPlayers) {
            m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
        }
    } else {
        logger.debug("权限清理任务：没有过期的记录需要删除。");
    }
}
/**
 * @brief 获取玩家在某个权限组中的身份过期时间。
 * @param playerUuid 玩家的 UUID。
 * @param groupName 组名称。
 * @return 可选的 long long，表示 Unix 时间戳（秒）。如果玩家不在组中或身份永不过期，则返回 std::nullopt。
 */
std::optional<long long> PermissionManager::PermissionManagerImpl::getPlayerGroupExpirationTime(
    const std::string& playerUuid,
    const std::string& groupName
) {
    // 利用现有缓存机制，getPlayerGroupsWithPriorities 现在会返回带有过期时间的数据
    auto playerGroups = getPlayerGroupsWithPriorities(playerUuid);
    for (const auto& groupDetails : playerGroups) {
        if (groupDetails.name == groupName) {
            return groupDetails.expirationTime; // 直接返回缓存的过期时间
        }
    }
    return std::nullopt; // 玩家不在该组中
}

/**
 * @brief 设置玩家在某个权限组中的身份过期时间。
 * @param playerUuid 玩家的 UUID。
 * @param groupName 组名称。
 * @param durationSeconds 从现在开始的持续时间（秒）。小于或等于 0 的值表示永不过期。
 * @return 如果设置成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::setPlayerGroupExpirationTime(
    const std::string& playerUuid,
    const std::string& groupName,
    long long          durationSeconds
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) {
        return false; // 组不存在
    }

    std::optional<long long> expiryTimestamp;
    if (durationSeconds > 0) {
        long long currentTime =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        expiryTimestamp = currentTime + durationSeconds;
    }

    // 调用存储层更新
    if (m_storage->updatePlayerGroupExpirationTime(playerUuid, groupId, expiryTimestamp)) {
        // 更新成功后，使该玩家的缓存失效，以便下次获取时能得到最新数据
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
        return true;
    }

    // 更新失败通常意味着玩家不在组中。此API不负责添加玩家。
    ::ll::mod::NativeMod::current()->getLogger().warn(
        "设置玩家 '{}' 在组 '{}' 的过期时间失败，可能玩家不在此组中。",
        playerUuid,
        groupName
    );
    return false;
}

} // namespace permission
} // namespace BA
