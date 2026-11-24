#include "permission/PermissionManagerImpl.h" // 包含实现类的头文件
#include "db/IDatabase.h"                     // 包含数据库接口
#include "ll/api/io/Logger.h"                 // 包含日志库
#include "ll/api/mod/NativeMod.h"             // 包含原生模块接口
#include "permission/PermissionCache.h"       // 包含权限缓存
#include "permission/PermissionStorage.h"     // 包含权限存储
#include "permission/events/PlayerJoinGroupEvent.h" // 包含玩家加入组事件
#include "permission/events/PlayerLeaveGroupEvent.h" // 包含玩家离开组事件
#include "permission/events/GroupPermissionChangeEvent.h" // 包含组权限变更事件
#include <algorithm>                          // 包含算法库
#include <functional>                         // 包含函数对象库
#include <mutex>
#include <ll/api/event/EventBus.h>            // 包含事件总线



namespace BA {
namespace permission {

using namespace std;

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

    (void)threadPoolSize; // 保留参数以维持 ABI 兼容；缓存改为同步失效 + epoch 校验
    try {
        // 创建权限存储和缓存实例
        m_storage = make_unique<internal::PermissionStorage>(db);
        m_cache   = make_unique<internal::PermissionCache>();

        // 确保数据库表存在
        m_storage->ensureTables();

        // 继承图和默认权限均为权威内存索引，即使关闭预热也需全量加载。
        auto parentToChildren = m_storage->fetchAllInheritance();
        unordered_map<string, set<string>> childToParents;
        for (const auto& [parent, children] : parentToChildren) {
            for (const auto& child : children) {
                childToParents[child].insert(parent);
            }
        }
        m_cache->populateInheritance(std::move(parentToChildren), std::move(childToParents));
        m_cache->populateAllPermissionDefaults(m_storage->fetchAllPermissionDefaults());

        // 如果启用预热，则填充组映射和组权限派生缓存
        if (enableWarmup) {
            populateAllCaches();
        }

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
        return;
    }
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
    const size_t                                 groupCount   = groupNameMap.size();
    m_cache->populateAllGroups(std::move(groupNameMap));
    logger.debug("已使用 {} 个条目填充组名缓存。", groupCount);

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
        return *cachedId;
    }

    // 2. 缓存未命中，记录版本后从存储层查询，防止删组后回填旧ID
    const uint64_t epoch    = m_cache->groupMappingEpoch();
    std::string    idFromDb = m_storage->fetchGroupIdByName(groupName);

    // 3. 如果在存储层找到且版本未变，则回填缓存
    if (!idFromDb.empty()) {
        m_cache->storeGroupIfEpoch(groupName, idFromDb, epoch);
    }
    return idFromDb;
}

std::vector<CompiledPermissionRule>
PermissionManager::PermissionManagerImpl::compilePermissionsForGroups(
    const std::set<std::string>& relevantGroupNames,
    std::map<std::string, bool>  effectiveState
) {
    if (!relevantGroupNames.empty()) {
        auto                      relevantGroupsMap = m_storage->fetchGroupDetailsByNames(relevantGroupNames);
        std::vector<GroupDetails> relevantGroups;
        std::vector<std::string>  relevantGroupIds;
        relevantGroups.reserve(relevantGroupsMap.size());
        relevantGroupIds.reserve(relevantGroupsMap.size());

        for (const auto& name : relevantGroupNames) {
            auto it = relevantGroupsMap.find(name);
            if (it != relevantGroupsMap.end()) {
                relevantGroups.push_back(it->second);
                relevantGroupIds.push_back(it->second.id);
            }
        }

        std::sort(relevantGroups.begin(), relevantGroups.end(), [](const auto& a, const auto& b) {
            return a.priority < b.priority;
        });

        auto allDirectPermissions = m_storage->fetchDirectPermissionsOfGroups(relevantGroupIds);
        for (const auto& group : relevantGroups) {
            auto it = allDirectPermissions.find(group.id);
            if (it != allDirectPermissions.end()) {
                for (const auto& rule : it->second) {
                    bool        isNegated = rule.starts_with("-");
                    std::string baseName  = isNegated ? rule.substr(1) : rule;
                    if (baseName.empty()) continue;
                    effectiveState[baseName] = !isNegated;
                }
            }
        }
    }

    std::vector<CompiledPermissionRule> finalPerms;
    finalPerms.reserve(effectiveState.size());
    for (const auto& [pattern, state] : effectiveState) {
        try {
            finalPerms.emplace_back(pattern, wildcardToRegex(pattern), state);
        } catch (const std::regex_error& e) {
            ::ll::mod::NativeMod::current()->getLogger().error("权限模式 '{}' 无效: {}", pattern, e.what());
        }
    }

    std::sort(finalPerms.begin(), finalPerms.end(), [](const auto& a, const auto& b) {
        return a.pattern.length() > b.pattern.length();
    });

    return finalPerms;
}

void PermissionManager::PermissionManagerImpl::invalidateGroupDerivedCaches(const std::string& groupName) {
    invalidateGroupDerivedCaches(m_cache->getChildGroupsRecursive(groupName));
}

void PermissionManager::PermissionManagerImpl::invalidateGroupDerivedCaches(
    const std::set<std::string>& affectedGroups
) {
    m_cache->invalidateGroupPermissionsForGroups(affectedGroups);
    m_cache->invalidatePlayerPermissionsForGroups(affectedGroups);
}

void PermissionManager::PermissionManagerImpl::invalidatePlayerCaches(const std::string& playerUuid) {
    // 必须先失效玩家组缓存再失效玩家权限缓存，保证并发权限计算无法用旧组缓存回填新权限缓存。
    m_cache->invalidatePlayerGroups(playerUuid);
    m_cache->invalidatePlayerPermissions(playerUuid);
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
        m_cache->invalidateAllPlayerPermissions();
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
        m_cache->invalidateGroupPermissions(groupName);
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
    std::lock_guard graphLock(m_graphWriteMutex);
    string          groupId = getCachedGroupId(groupName);
    if (groupId.empty()) {
        return false; // 组不存在
    }

    const auto affectedGroups = m_cache->getChildGroupsRecursive(groupName);
    if (!m_storage->deleteGroup(groupId)) return false;

    m_cache->removeGroupFromInheritance(groupName);
    m_cache->invalidateGroup(groupName);
    // 先失效玩家组缓存，再失效组权限与玩家权限缓存
    m_cache->invalidatePlayerGroupsForGroup(groupName);
    invalidateGroupDerivedCaches(affectedGroups);
    return true;
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
    if (m_storage->updateGroupDescription(groupName, newDescription)) {
        m_cache->invalidatePlayerGroupsForGroup(groupName);
        return true;
    }
    return false;
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
        invalidateGroupDerivedCaches(groupName);

        ll::event::EventBus::getInstance().publish(
            event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent)
        );
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
        invalidateGroupDerivedCaches(groupName);

        ll::event::EventBus::getInstance().publish(
            event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent)
        );
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
    if (auto cachedPerms = m_cache->findGroupPermissions(groupName)) {
        return *cachedPerms;
    }

    // 2. 缓存未命中，先记录版本再查图与数据库，避免并发写后回填陈旧结果
    const uint64_t epoch = m_cache->groupPermissionsEpoch();
    if (getCachedGroupId(groupName).empty()) {
        return {}; // 不为不存在的组写入空缓存
    }

    std::set<std::string> ancestorNames = m_cache->getAllAncestorGroups(groupName);
    auto                  finalPerms    = compilePermissionsForGroups(ancestorNames, {});

    // 3. 版本匹配时回填缓存
    m_cache->storeGroupPermissions(groupName, finalPerms, epoch);
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
            invalidateGroupDerivedCaches(groupName);
            ll::event::EventBus::getInstance().publish(
                event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent)
            );
        }
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
            invalidateGroupDerivedCaches(groupName);
            ll::event::EventBus::getInstance().publish(
                event::GroupPermissionChangeAfterEvent(groupNameForEvent, permissionRuleForEvent, isAddForEvent)
            );
        }
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
    std::lock_guard graphLock(m_graphWriteMutex);
    if (!m_cache->canAddInheritance(groupName, parentGroupName)) {
        return false; // 检测到循环或无效继承
    }
    string groupId       = getCachedGroupId(groupName);
    string parentGroupId = getCachedGroupId(parentGroupName);
    if (groupId.empty() || parentGroupId.empty()) return false; // 组或父组不存在

    if (m_storage->addGroupInheritance(groupId, parentGroupId)) {
        m_cache->addInheritance(groupName, parentGroupName);
        invalidateGroupDerivedCaches(groupName);
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
    std::lock_guard graphLock(m_graphWriteMutex);
    string          groupId       = getCachedGroupId(groupName);
    string          parentGroupId = getCachedGroupId(parentGroupName);
    if (groupId.empty() || parentGroupId.empty()) return false; // 组或父组不存在

    if (m_storage->removeGroupInheritance(groupId, parentGroupId)) {
        m_cache->removeInheritance(groupName, parentGroupName);
        invalidateGroupDerivedCaches(groupName);
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
    return m_cache->getDirectParentGroups(groupName);
}

/**
 * @brief 设置组的优先级。
 * @param groupName 组名称。
 * @param priority 新的优先级值。
 * @return 如果设置成功则返回 true，否则返回 false。
 */
bool PermissionManager::PermissionManagerImpl::setGroupPriority(const std::string& groupName, int priority) {
    if (m_storage->updateGroupPriority(groupName, priority)) {
        m_cache->invalidatePlayerGroupsForGroup(groupName);
        invalidateGroupDerivedCaches(groupName);
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
        invalidatePlayerCaches(playerUuid);

        // 触发 PlayerJoinGroupAfterEvent
        ll::event::EventBus::getInstance().publish(
            event::PlayerJoinGroupAfterEvent(playerUuidForEvent, groupNameForEvent, expiryTimestampForEvent)
        );
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
        invalidatePlayerCaches(playerUuid);

        ll::event::EventBus::getInstance().publish(
            event::PlayerLeaveGroupAfterEvent(playerUuidForEvent, groupNameForEvent)
        );
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
    if (auto cachedGroups = m_cache->findPlayerGroups(playerUuid)) {
        return *cachedGroups;
    }

    // 2. 缓存未命中，先记录版本再查库，版本一致时回填
    const uint64_t epoch        = m_cache->playerGroupsEpoch();
    auto           groupsFromDb = m_storage->fetchPlayerGroupsWithDetails(playerUuid);
    m_cache->storePlayerGroups(playerUuid, groupsFromDb, epoch);
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
    return getPlayerPermissionRules(playerUuid)->rules;
}

std::shared_ptr<const internal::PlayerPermissionRules>
PermissionManager::PermissionManagerImpl::getPlayerPermissionRules(const std::string& playerUuid) {
    // 1. 检查缓存
    if (auto cachedPerms = m_cache->findPlayerPermissions(playerUuid)) {
        return cachedPerms;
    }

    // 2. 在读取任何数据前记录版本，防止并发写失效后回填陈旧结果
    const uint64_t permEpoch = m_cache->playerPermissionsEpoch();

    // 2.1 添加默认权限（init 已全量加载；未加载时按需补全）
    if (!m_cache->arePermissionDefaultsLoaded()) {
        m_cache->populateAllPermissionDefaults(m_storage->fetchAllPermissionDefaults());
    }
    map<string, bool> effectiveState;
    for (const auto& [name, state] : m_cache->getAllPermissionDefaults()) {
        if (state) {
            effectiveState[name] = true;
        }
    }

    // 2.2 获取所有相关组（直接所属和继承）及其权限
    auto                     playerGroups = getPlayerGroupsWithPriorities(playerUuid);
    std::optional<long long> earliestExpiry;
    std::set<std::string>    allRelevantGroupNames;
    for (const auto& group : playerGroups) {
        if (group.expirationTime && (!earliestExpiry || *group.expirationTime < *earliestExpiry)) {
            earliestExpiry = group.expirationTime;
        }
        auto ancestors = m_cache->getAllAncestorGroups(group.name);
        allRelevantGroupNames.insert(ancestors.begin(), ancestors.end());
    }

    auto finalPerms = std::make_shared<const internal::PlayerPermissionRules>(
        compilePermissionsForGroups(allRelevantGroupNames, std::move(effectiveState))
    );

    // 3. 版本匹配时回填缓存
    std::vector<std::string> sourceGroups(allRelevantGroupNames.begin(), allRelevantGroupNames.end());
    m_cache->storePlayerPermissions(playerUuid, finalPerms, earliestExpiry, std::move(sourceGroups), permEpoch);
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
    if (auto state = getPlayerPermissionRules(playerUuid)->match(permissionNode)) {
        return *state;
    }

    // 如果没有匹配到特定规则，则回退到权限的默认值（权威索引已在 init 时全量加载）
    if (!m_cache->arePermissionDefaultsLoaded()) {
        m_cache->populateAllPermissionDefaults(m_storage->fetchAllPermissionDefaults());
    }
    if (auto defaultValue = m_cache->findPermissionDefault(permissionNode)) {
        return *defaultValue;
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
        invalidatePlayerCaches(playerUuid);
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
        invalidatePlayerCaches(playerUuid);
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
            invalidatePlayerCaches(playerUuid);
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
        invalidatePlayerCaches(playerUuid);
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
