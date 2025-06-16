
#include "permission/PermissionCache.h"
#include <queue>

namespace BA {
namespace permission {
namespace internal {

using namespace std;

// --- Group Name/ID Cache ---
optional<string> PermissionCache::findGroupId(const string& groupName) {
    shared_lock<shared_mutex> lock(m_groupNameMutex);
    auto                      it = m_groupNameCache.find(groupName);
    if (it != m_groupNameCache.end()) {
        return it->second;
    }
    return nullopt;
}

optional<string> PermissionCache::findGroupName(const string& groupId) {
    shared_lock<shared_mutex> lock(m_groupIdMutex);
    auto                      it = m_groupIdCache.find(groupId);
    if (it != m_groupIdCache.end()) {
        return it->second;
    }
    return nullopt;
}


void PermissionCache::storeGroup(const string& groupName, const string& groupId) {
    unique_lock<shared_mutex> nameLock(m_groupNameMutex);
    unique_lock<shared_mutex> idLock(m_groupIdMutex);

    // 如果旧的组名存在，先移除旧的ID映射
    auto it = m_groupNameCache.find(groupName);
    if (it != m_groupNameCache.end()) {
        m_groupIdCache.erase(it->second);
    }

    m_groupNameCache[groupName] = groupId;
    m_groupIdCache[groupId]     = groupName;
}

void PermissionCache::invalidateGroup(const string& groupName) {
    unique_lock<shared_mutex> nameLock(m_groupNameMutex);
    unique_lock<shared_mutex> idLock(m_groupIdMutex);

    auto it = m_groupNameCache.find(groupName);
    if (it != m_groupNameCache.end()) {
        m_groupIdCache.erase(it->second); // 移除ID到名称的映射
        m_groupNameCache.erase(it);       // 移除名称到ID的映射
    }
}

void PermissionCache::populateAllGroups(unordered_map<string, string>&& groupNameMap) {
    unique_lock<shared_mutex> nameLock(m_groupNameMutex);
    unique_lock<shared_mutex> idLock(m_groupIdMutex);

    m_groupNameCache.clear();
    m_groupIdCache.clear();

    for (auto& pair : groupNameMap) {
        m_groupNameCache[pair.first] = pair.second;
        m_groupIdCache[pair.second]  = pair.first;
    }
}

const std::unordered_map<std::string, std::string>& PermissionCache::getAllGroups() const {
    shared_lock<shared_mutex> lock(m_groupNameMutex);
    return m_groupNameCache;
}

// --- Player Permissions Cache ---
optional<vector<CompiledPermissionRule>> PermissionCache::findPlayerPermissions(const string& playerUuid) {
    shared_lock<shared_mutex> lock(m_playerPermissionsMutex);
    auto                      it = m_playerPermissionsCache.find(playerUuid);
    if (it != m_playerPermissionsCache.end()) {
        return it->second;
    }
    return nullopt;
}

void PermissionCache::storePlayerPermissions(
    const string&                         playerUuid,
    const vector<CompiledPermissionRule>& permissions
) {
    unique_lock<shared_mutex> lock(m_playerPermissionsMutex);
    m_playerPermissionsCache[playerUuid] = permissions;
}

void PermissionCache::invalidatePlayerPermissions(const string& playerUuid) {
    unique_lock<shared_mutex> lock(m_playerPermissionsMutex);
    m_playerPermissionsCache.erase(playerUuid);
}

void PermissionCache::invalidateAllPlayerPermissions() {
    unique_lock<shared_mutex> lock(m_playerPermissionsMutex);
    m_playerPermissionsCache.clear();
}

// --- Player Groups Cache ---
optional<vector<GroupDetails>> PermissionCache::findPlayerGroups(const string& playerUuid) {
    shared_lock<shared_mutex> lock(m_playerGroupsMutex);
    auto                      it = m_playerGroupsCache.find(playerUuid);
    if (it != m_playerGroupsCache.end()) {
        return it->second;
    }
    return nullopt;
}

void PermissionCache::storePlayerGroups(const string& playerUuid, const vector<GroupDetails>& groups) {
    unique_lock<shared_mutex> lock(m_playerGroupsMutex);
    m_playerGroupsCache[playerUuid] = groups;
}

void PermissionCache::invalidatePlayerGroups(const string& playerUuid) {
    unique_lock<shared_mutex> lock(m_playerGroupsMutex);
    m_playerGroupsCache.erase(playerUuid);
}

// --- Group Permissions Cache ---
optional<vector<CompiledPermissionRule>> PermissionCache::findGroupPermissions(const string& groupName) {
    shared_lock<shared_mutex> lock(m_groupPermissionsMutex);
    auto                      it = m_groupPermissionsCache.find(groupName);
    if (it != m_groupPermissionsCache.end()) {
        return it->second;
    }
    return nullopt;
}

void PermissionCache::storeGroupPermissions(
    const string&                         groupName,
    const vector<CompiledPermissionRule>& permissions
) {
    unique_lock<shared_mutex> lock(m_groupPermissionsMutex);
    m_groupPermissionsCache[groupName] = permissions;
}

void PermissionCache::invalidateGroupPermissions(const string& groupName) {
    unique_lock<shared_mutex> lock(m_groupPermissionsMutex);
    m_groupPermissionsCache.erase(groupName);
}

void PermissionCache::invalidateAllGroupPermissions() {
    unique_lock<shared_mutex> lock(m_groupPermissionsMutex);
    m_groupPermissionsCache.clear();
}

// --- Permission Defaults Cache ---
optional<bool> PermissionCache::findPermissionDefault(const string& permissionName) {
    shared_lock<shared_mutex> lock(m_permissionDefaultsMutex);
    auto                      it = m_permissionDefaultsCache.find(permissionName);
    if (it != m_permissionDefaultsCache.end()) {
        return it->second;
    }
    return nullopt;
}

void PermissionCache::storePermissionDefault(const string& permissionName, bool defaultValue) {
    unique_lock<shared_mutex> lock(m_permissionDefaultsMutex);
    m_permissionDefaultsCache[permissionName] = defaultValue;
}

void PermissionCache::populateAllPermissionDefaults(unordered_map<string, bool>&& defaultsMap) {
    unique_lock<shared_mutex> lock(m_permissionDefaultsMutex);
    m_permissionDefaultsCache = std::move(defaultsMap);
}

const std::unordered_map<std::string, bool>& PermissionCache::getAllPermissionDefaults() const {
    shared_lock<shared_mutex> lock(m_permissionDefaultsMutex);
    return m_permissionDefaultsCache;
}

// --- Inheritance Cache ---
void PermissionCache::populateInheritance(
    unordered_map<string, set<string>>&& parentToChildren,
    unordered_map<string, set<string>>&& childToParents
) {
    unique_lock<shared_mutex> lock(m_inheritanceMutex);
    m_parentToChildren = std::move(parentToChildren);
    m_childToParents   = std::move(childToParents);
}

void PermissionCache::addInheritance(const string& child, const string& parent) {
    unique_lock<shared_mutex> lock(m_inheritanceMutex);
    m_childToParents[child].insert(parent);
    m_parentToChildren[parent].insert(child);
}

void PermissionCache::removeInheritance(const string& child, const string& parent) {
    unique_lock<shared_mutex> lock(m_inheritanceMutex);
    if (m_childToParents.count(child)) {
        m_childToParents[child].erase(parent);
        if (m_childToParents[child].empty()) {
            m_childToParents.erase(child);
        }
    }
    if (m_parentToChildren.count(parent)) {
        m_parentToChildren[parent].erase(child);
        if (m_parentToChildren[parent].empty()) {
            m_parentToChildren.erase(parent);
        }
    }
}

bool PermissionCache::hasPath(const string& startNode, const string& endNode) const {
    if (startNode == endNode) return true;
    shared_lock<shared_mutex> lock(m_inheritanceMutex);

    queue<string> q;
    set<string>   visited;

    q.push(startNode);
    visited.insert(startNode);

    while (!q.empty()) {
        string current = q.front();
        q.pop();

        auto it = m_parentToChildren.find(current);
        if (it != m_parentToChildren.end()) {
            for (const auto& neighbor : it->second) {
                if (neighbor == endNode) return true;
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    q.push(neighbor);
                }
            }
        }
    }
    return false;
}

set<string> PermissionCache::getAllAncestorGroups(const string& groupName) const {
    shared_lock<shared_mutex> lock(m_inheritanceMutex);
    set<string>               ancestors;
    queue<string>             q;

    q.push(groupName);
    ancestors.insert(groupName);

    while (!q.empty()) {
        string current = q.front();
        q.pop();
        auto it = m_childToParents.find(current);
        if (it != m_childToParents.end()) {
            for (const auto& parent : it->second) {
                if (ancestors.find(parent) == ancestors.end()) {
                    ancestors.insert(parent);
                    q.push(parent);
                }
            }
        }
    }
    return ancestors;
}

set<string> PermissionCache::getChildGroupsRecursive(const string& groupName) const {
    shared_lock<shared_mutex> lock(m_inheritanceMutex);
    set<string>               children;
    queue<string>             q;

    q.push(groupName);
    children.insert(groupName);

    while (!q.empty()) {
        string current = q.front();
        q.pop();
        auto it = m_parentToChildren.find(current);
        if (it != m_parentToChildren.end()) {
            for (const auto& child : it->second) {
                if (children.find(child) == children.end()) {
                    children.insert(child);
                    q.push(child);
                }
            }
        }
    }
    return children;
}


} // namespace internal
} // namespace permission
} // namespace BA
