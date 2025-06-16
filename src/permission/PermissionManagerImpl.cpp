#include "permission/PermissionManagerImpl.h"
#include "db/IDatabase.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include "permission/AsyncCacheInvalidator.h"
#include "permission/PermissionCache.h"
#include "permission/PermissionStorage.h"
#include <algorithm>
#include <functional>



namespace BA {
namespace permission {

using namespace std;

// --- Helper function: wildcard to regex (moved from the original class) ---
std::regex PermissionManager::PermissionManagerImpl::wildcardToRegex(const std::string& pattern) {
    std::string regexPatternStr = "^";
    for (char c : pattern) {
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
    // Using ECMAScript and case-insensitive matching
    return std::regex(regexPatternStr, std::regex_constants::ECMAScript | std::regex_constants::icase);
}

// --- Implementation Class Lifecycle ---

PermissionManager::PermissionManagerImpl::PermissionManagerImpl() = default;
PermissionManager::PermissionManagerImpl::~PermissionManagerImpl() {
    // Ensure shutdown is called, especially if the user forgets.
    if (m_initialized) {
        shutdown();
    }
}

bool PermissionManager::PermissionManagerImpl::init(db::IDatabase* db, bool enableWarmup, unsigned int threadPoolSize) {
    if (m_initialized) {
        ::ll::mod::NativeMod::current()->getLogger().warn("PermissionManager already initialized.");
        return true;
    }
    if (!db) {
        ::ll::mod::NativeMod::current()->getLogger().error("Initialization failed: Database pointer is null.");
        return false;
    }

    try {
        m_storage     = make_unique<internal::PermissionStorage>(db);
        m_cache       = make_unique<internal::PermissionCache>();
        m_invalidator = make_unique<internal::AsyncCacheInvalidator>(*m_cache, *m_storage);

        m_storage->ensureTables();

        if (enableWarmup) {
            populateAllCaches();
        }

        m_invalidator->start(threadPoolSize);
        m_initialized = true;
        ::ll::mod::NativeMod::current()->getLogger().info("PermissionManager initialized successfully.");
        return true;
    } catch (const std::exception& e) {
        ::ll::mod::NativeMod::current()->getLogger().error("PermissionManager initialization failed: {}", e.what());
        return false;
    }
}

void PermissionManager::PermissionManagerImpl::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_invalidator->stop();
    m_initialized = false;
    ::ll::mod::NativeMod::current()->getLogger().info("PermissionManager has been shut down.");
}

void PermissionManager::PermissionManagerImpl::populateAllCaches() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();
    logger.info("Warming up permission caches...");

    // Populate Group Name -> ID cache
    std::vector<std::string> groupNamesVec = m_storage->fetchAllGroupNames();
    std::set<std::string> groupNamesSet(groupNamesVec.begin(), groupNamesVec.end()); // Convert vector to set
    
    // Use bulk fetch to get all group names and their IDs
    std::unordered_map<std::string, std::string> groupNameMap = m_storage->fetchGroupIdsByNames(groupNamesSet);
    m_cache->populateAllGroups(std::move(groupNameMap));
    logger.debug("Populated group name cache with {} entries.", groupNameMap.size()); // Use map size for accuracy

    // Populate Inheritance cache
    auto                               parentToChildren = m_storage->fetchAllInheritance();
    unordered_map<string, set<string>> childToParents;
    for (const auto& pair : parentToChildren) {
        const string& parent = pair.first;
        for (const string& child : pair.second) {
            childToParents[child].insert(parent);
        }
    }
    m_cache->populateInheritance(std::move(parentToChildren), std::move(childToParents));
    logger.debug("Populated inheritance cache.");

    // Populate Permission Defaults cache
    m_cache->populateAllPermissionDefaults(m_storage->fetchAllPermissionDefaults());
    logger.debug("Populated permission defaults cache.");

    // Group permissions cache is populated on demand by getPermissionsOfGroup
    // Player permissions and groups caches are also on-demand

    logger.info("Permission cache warmup complete.");
}

// --- Cache-aware helper ---
std::string PermissionManager::PermissionManagerImpl::getCachedGroupId(const std::string& groupName) {
    // 1. Check cache
    auto cachedId = m_cache->findGroupId(groupName);
    if (cachedId) {
        return *cachedId;
    }

    // 2. Cache miss, query storage
    std::string idFromDb = m_storage->fetchGroupIdByName(groupName);

    // 3. Store in cache if found
    if (!idFromDb.empty()) {
        m_cache->storeGroup(groupName, idFromDb);
    }
    return idFromDb;
}

// --- Public API Implementations ---

bool PermissionManager::PermissionManagerImpl::registerPermission(
    const std::string& name,
    const std::string& description,
    bool               defaultValue
) {
    if (m_storage->upsertPermission(name, description, defaultValue)) {
        m_cache->storePermissionDefault(name, defaultValue);
        m_invalidator->enqueueTask({CacheInvalidationTaskType::ALL_GROUPS_MODIFIED, ""});
        m_invalidator->enqueueTask({CacheInvalidationTaskType::ALL_PLAYERS_MODIFIED, ""});
        return true;
    }
    return false;
}

bool PermissionManager::PermissionManagerImpl::permissionExists(const std::string& name) {
    return m_storage->permissionExists(name);
}

std::vector<std::string> PermissionManager::PermissionManagerImpl::getAllPermissions() {
    return m_storage->fetchAllPermissionNames();
}

bool PermissionManager::PermissionManagerImpl::createGroup(
    const std::string& groupName,
    const std::string& description
) {
    string groupId;
    if (m_storage->createGroup(groupName, description, groupId) && !groupId.empty()) {
        m_cache->storeGroup(groupName, groupId);
        // No need to invalidate, it's a new group with no perms or players.
        return true;
    }
    return false;
}

bool PermissionManager::PermissionManagerImpl::deleteGroup(const std::string& groupName) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) {
        return false;
    }

    // Invalidate all inheritance related to this group
    auto parents  = m_cache->getAllAncestorGroups(groupName);
    auto children = m_cache->getChildGroupsRecursive(groupName);

    if (m_storage->deleteGroup(groupId)) {
        // Invalidate caches after deleting from DB
        m_cache->invalidateGroup(groupName);
        // Enqueue invalidation for all related groups and their players
        for (const auto& child : children)
            m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, child});
        return true;
    }
    return false;
}

bool PermissionManager::PermissionManagerImpl::groupExists(const std::string& groupName) {
    return !getCachedGroupId(groupName).empty();
}

std::vector<std::string> PermissionManager::PermissionManagerImpl::getAllGroups() {
    return m_storage->fetchAllGroupNames();
}

GroupDetails PermissionManager::PermissionManagerImpl::getGroupDetails(const std::string& groupName) {
    return m_storage->fetchGroupDetails(groupName);
}

bool PermissionManager::PermissionManagerImpl::updateGroupDescription(
    const std::string& groupName,
    const std::string& newDescription
) {
    return m_storage->updateGroupDescription(groupName, newDescription);
}

std::string PermissionManager::PermissionManagerImpl::getGroupDescription(const std::string& groupName) {
    return m_storage->fetchGroupDescription(groupName);
}

bool PermissionManager::PermissionManagerImpl::addPermissionToGroup(
    const std::string& groupName,
    const std::string& permissionRule
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false;
    if (m_storage->addPermissionToGroup(groupId, permissionRule)) {
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

bool PermissionManager::PermissionManagerImpl::removePermissionFromGroup(
    const std::string& groupName,
    const std::string& permissionRule
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false;
    if (m_storage->removePermissionFromGroup(groupId, permissionRule)) {
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

std::vector<std::string>
PermissionManager::PermissionManagerImpl::getDirectPermissionsOfGroup(const std::string& groupName) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return {};
    return m_storage->fetchDirectPermissionsOfGroup(groupId);
}

// Complex logic remains here
std::vector<CompiledPermissionRule>
PermissionManager::PermissionManagerImpl::getPermissionsOfGroup(const std::string& groupName) {
    // 1. Check cache
    auto cachedPerms = m_cache->findGroupPermissions(groupName);
    if (cachedPerms) {
        return *cachedPerms;
    }

    // 2. Cache miss, calculate
    set<string>          ancestorNames = m_cache->getAllAncestorGroups(groupName);
    vector<GroupDetails> relevantGroups;
    for (const auto& name : ancestorNames) {
        auto details = getGroupDetails(name);
        if (details.isValid) {
            relevantGroups.push_back(details);
        }
    }

    sort(relevantGroups.begin(), relevantGroups.end(), [](const auto& a, const auto& b) {
        return a.priority < b.priority;
    });

    map<string, bool> effectiveState;
    for (const auto& group : relevantGroups) {
        auto rules = m_storage->fetchDirectPermissionsOfGroup(group.id);
        for (const auto& rule : rules) {
            bool   isNegated = rule.starts_with('-');
            string baseName  = isNegated ? rule.substr(1) : rule;
            if (baseName.empty()) continue;
            effectiveState[baseName] = !isNegated;
        }
    }

    vector<CompiledPermissionRule> finalPerms;
    for (const auto& [pattern, state] : effectiveState) {
        try {
            finalPerms.emplace_back(pattern, wildcardToRegex(pattern), state);
        } catch (const std::regex_error& e) {
            ::ll::mod::NativeMod::current()->getLogger().error("Regex error for pattern '{}': {}", pattern, e.what());
        }
    }

    sort(finalPerms.begin(), finalPerms.end(), [](const auto& a, const auto& b) {
        return a.pattern.length() > b.pattern.length();
    });

    // 3. Store in cache and return
    m_cache->storeGroupPermissions(groupName, finalPerms);
    return finalPerms;
}

size_t PermissionManager::PermissionManagerImpl::addPermissionsToGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return 0;
    size_t count = m_storage->addPermissionsToGroup(groupId, permissionRules);
    if (count > 0) {
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
    }
    return count;
}

size_t PermissionManager::PermissionManagerImpl::removePermissionsFromGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return 0;
    size_t count = m_storage->removePermissionsFromGroup(groupId, permissionRules);
    if (count > 0) {
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
    }
    return count;
}

bool PermissionManager::PermissionManagerImpl::addGroupInheritance(
    const std::string& groupName,
    const std::string& parentGroupName
) {
    if (groupName == parentGroupName || m_cache->hasPath(parentGroupName, groupName)) {
        return false; // Cycle detected
    }
    string groupId       = getCachedGroupId(groupName);
    string parentGroupId = getCachedGroupId(parentGroupName);
    if (groupId.empty() || parentGroupId.empty()) return false;

    if (m_storage->addGroupInheritance(groupId, parentGroupId)) {
        m_cache->addInheritance(groupName, parentGroupName);
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

bool PermissionManager::PermissionManagerImpl::removeGroupInheritance(
    const std::string& groupName,
    const std::string& parentGroupName
) {
    string groupId       = getCachedGroupId(groupName);
    string parentGroupId = getCachedGroupId(parentGroupName);
    if (groupId.empty() || parentGroupId.empty()) return false;

    if (m_storage->removeGroupInheritance(groupId, parentGroupId)) {
        m_cache->removeInheritance(groupName, parentGroupName);
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

std::vector<std::string> PermissionManager::PermissionManagerImpl::getAllAncestorGroups(const std::string& groupName) {
    auto ancestors = m_cache->getAllAncestorGroups(groupName);
    ancestors.erase(groupName); // 移除自身
    return std::vector<std::string>(ancestors.begin(), ancestors.end());
}

std::vector<std::string> PermissionManager::PermissionManagerImpl::getDirectParentGroups(const std::string& groupName) {
    std::vector<std::string> directParents;
    std::string              groupId = getCachedGroupId(groupName);
    if (groupId.empty()) {
        return directParents;
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

bool PermissionManager::PermissionManagerImpl::setGroupPriority(const std::string& groupName, int priority) {
    if (m_storage->updateGroupPriority(groupName, priority)) {
        m_invalidator->enqueueTask({CacheInvalidationTaskType::GROUP_MODIFIED, groupName});
        return true;
    }
    return false;
}

int PermissionManager::PermissionManagerImpl::getGroupPriority(const std::string& groupName) {
    return m_storage->fetchGroupPriority(groupName);
}

bool PermissionManager::PermissionManagerImpl::addPlayerToGroup(
    const std::string& playerUuid,
    const std::string& groupName
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false;
    if (m_storage->addPlayerToGroup(playerUuid, groupId)) {
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
        return true;
    }
    return false;
}

bool PermissionManager::PermissionManagerImpl::removePlayerFromGroup(
    const std::string& playerUuid,
    const std::string& groupName
) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return false;
    if (m_storage->removePlayerFromGroup(playerUuid, groupId)) {
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
        return true;
    }
    return false;
}

std::vector<GroupDetails>
PermissionManager::PermissionManagerImpl::getPlayerGroupsWithPriorities(const std::string& playerUuid) {
    auto cachedGroups = m_cache->findPlayerGroups(playerUuid);
    if (cachedGroups) {
        return *cachedGroups;
    }
    auto groupsFromDb = m_storage->fetchPlayerGroupsWithDetails(playerUuid);
    m_cache->storePlayerGroups(playerUuid, groupsFromDb);
    return groupsFromDb;
}

std::vector<std::string> PermissionManager::PermissionManagerImpl::getPlayerGroups(const std::string& playerUuid) {
    auto           details = getPlayerGroupsWithPriorities(playerUuid);
    vector<string> names;
    transform(details.begin(), details.end(), back_inserter(names), [](const auto& d) { return d.name; });
    return names;
}

std::vector<std::string> PermissionManager::PermissionManagerImpl::getPlayerGroupIds(const std::string& playerUuid) {
    auto           details = getPlayerGroupsWithPriorities(playerUuid);
    vector<string> ids;
    transform(details.begin(), details.end(), back_inserter(ids), [](const auto& d) { return d.id; });
    return ids;
}

std::vector<std::string> PermissionManager::PermissionManagerImpl::getPlayersInGroup(const std::string& groupName) {
    string groupId = getCachedGroupId(groupName);
    if (groupId.empty()) return {};
    return m_storage->fetchPlayersInGroup(groupId);
}

std::vector<CompiledPermissionRule>
PermissionManager::PermissionManagerImpl::getAllPermissionsForPlayer(const std::string& playerUuid) {
    auto cachedPerms = m_cache->findPlayerPermissions(playerUuid);
    if (cachedPerms) {
        return *cachedPerms;
    }

    // Calculation logic
    map<string, bool> effectiveState;
    // 1. Add default permissions
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

    // 2. Get all groups (direct + inherited) and their permissions
    auto        playerGroups = getPlayerGroupsWithPriorities(playerUuid);
    set<string> allRelevantGroupNames;
    for (const auto& group : playerGroups) {
        auto ancestors = m_cache->getAllAncestorGroups(group.name);
        allRelevantGroupNames.insert(ancestors.begin(), ancestors.end());
    }

    vector<GroupDetails> allRelevantGroupsDetails;
    for (const auto& name : allRelevantGroupNames) {
        auto details = getGroupDetails(name);
        if (details.isValid) {
            allRelevantGroupsDetails.push_back(details);
        }
    }

    sort(allRelevantGroupsDetails.begin(), allRelevantGroupsDetails.end(), [](const auto& a, const auto& b) {
        return a.priority < b.priority;
    });

    for (const auto& group : allRelevantGroupsDetails) {
        auto rules = m_storage->fetchDirectPermissionsOfGroup(group.id);
        for (const auto& rule : rules) {
            bool   isNegated = rule.starts_with('-');
            string baseName  = isNegated ? rule.substr(1) : rule;
            if (baseName.empty()) continue;
            effectiveState[baseName] = !isNegated;
        }
    }

    vector<CompiledPermissionRule> finalPerms;
    for (const auto& [pattern, state] : effectiveState) {
        try {
            finalPerms.emplace_back(pattern, wildcardToRegex(pattern), state);
        } catch (const std::regex_error& e) {
            ::ll::mod::NativeMod::current()->getLogger().error("Regex error for pattern '{}': {}", pattern, e.what());
        }
    }

    sort(finalPerms.begin(), finalPerms.end(), [](const auto& a, const auto& b) {
        return a.pattern.length() > b.pattern.length();
    });

    m_cache->storePlayerPermissions(playerUuid, finalPerms);
    return finalPerms;
}

bool PermissionManager::PermissionManagerImpl::hasPermission(
    const std::string& playerUuid,
    const std::string& permissionNode
) {
    auto rules = getAllPermissionsForPlayer(playerUuid);
    for (const auto& rule : rules) {
        if (regex_match(permissionNode, rule.regex)) {
            return rule.state;
        }
    }

    // Fallback to default value
    auto defaultValue = m_cache->findPermissionDefault(permissionNode);
    if (defaultValue) {
        return *defaultValue;
    }

    // Double check DB if not in cache (should be rare)
    auto defaults = m_storage->fetchAllPermissionDefaults();
    auto it       = defaults.find(permissionNode);
    if (it != defaults.end()) {
        m_cache->storePermissionDefault(permissionNode, it->second);
        return it->second;
    }

    return false; // Default deny if permission is not registered
}

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
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
    }
    return count;
}

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
        m_invalidator->enqueueTask({CacheInvalidationTaskType::PLAYER_GROUP_CHANGED, playerUuid});
    }
    return count;
}


} // namespace permission
} // namespace BA
