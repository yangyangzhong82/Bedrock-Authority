#include "permission/PermissionCache.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

namespace BA {
namespace permission {
namespace internal {

namespace {

long long currentUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

template <typename Map>
void evictOldestToTarget(Map& cache, size_t targetSize) {
    if (cache.size() <= targetSize) return;

    std::vector<std::pair<uint64_t, typename Map::key_type>> order;
    order.reserve(cache.size());
    for (const auto& [key, entry] : cache) {
        order.emplace_back(entry.insertedSeq, key);
    }

    const size_t evictCount = cache.size() - targetSize;
    std::nth_element(
        order.begin(),
        order.begin() + static_cast<std::ptrdiff_t>(evictCount),
        order.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; }
    );
    for (size_t i = 0; i < evictCount; ++i) {
        cache.erase(order[i].second);
    }
}

} // namespace

// --- 组名 <-> 组ID 映射 ---

uint64_t PermissionCache::groupMappingEpoch() const {
    std::shared_lock lock(m_groupMappingMutex);
    return m_groupMappingEpoch;
}

std::optional<std::string> PermissionCache::findGroupId(const std::string& groupName) const {
    std::shared_lock lock(m_groupMappingMutex);
    auto             it = m_groupNameCache.find(groupName);
    if (it != m_groupNameCache.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> PermissionCache::findGroupName(const std::string& groupId) const {
    std::shared_lock lock(m_groupMappingMutex);
    auto             it = m_groupIdCache.find(groupId);
    if (it != m_groupIdCache.end()) {
        return it->second;
    }
    return std::nullopt;
}

void PermissionCache::storeGroup(const std::string& groupName, const std::string& groupId) {
    std::unique_lock lock(m_groupMappingMutex);
    ++m_groupMappingEpoch;

    if (auto it = m_groupNameCache.find(groupName); it != m_groupNameCache.end()) {
        m_groupIdCache.erase(it->second);
    }
    if (auto it = m_groupIdCache.find(groupId); it != m_groupIdCache.end()) {
        m_groupNameCache.erase(it->second);
    }

    m_groupNameCache[groupName] = groupId;
    m_groupIdCache[groupId]     = groupName;
}

void PermissionCache::storeGroupIfEpoch(
    const std::string& groupName,
    const std::string& groupId,
    uint64_t           expectedEpoch
) {
    std::unique_lock lock(m_groupMappingMutex);
    if (m_groupMappingEpoch != expectedEpoch) return;

    if (auto it = m_groupNameCache.find(groupName); it != m_groupNameCache.end()) {
        m_groupIdCache.erase(it->second);
    }
    if (auto it = m_groupIdCache.find(groupId); it != m_groupIdCache.end()) {
        m_groupNameCache.erase(it->second);
    }

    m_groupNameCache[groupName] = groupId;
    m_groupIdCache[groupId]     = groupName;
}

void PermissionCache::invalidateGroup(const std::string& groupName) {
    std::unique_lock lock(m_groupMappingMutex);
    ++m_groupMappingEpoch;

    auto it = m_groupNameCache.find(groupName);
    if (it != m_groupNameCache.end()) {
        m_groupIdCache.erase(it->second);
        m_groupNameCache.erase(it);
    }
}

void PermissionCache::populateAllGroups(std::unordered_map<std::string, std::string>&& groupNameMap) {
    std::unique_lock lock(m_groupMappingMutex);
    ++m_groupMappingEpoch;

    m_groupNameCache.clear();
    m_groupIdCache.clear();

    for (auto& [name, id] : groupNameMap) {
        m_groupNameCache[name] = id;
        m_groupIdCache[id]     = name;
    }
}

std::unordered_map<std::string, std::string> PermissionCache::getAllGroups() const {
    std::shared_lock lock(m_groupMappingMutex);
    return m_groupNameCache;
}

// --- 玩家权限派生缓存 ---

PlayerPermissionRules::PlayerPermissionRules(std::vector<CompiledPermissionRule> compiled)
: rules(std::move(compiled)) {
    if (rules.empty()) return;
    const auto locale = rules.front().regex.getloc();
    traits.imbue(locale);
    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        if (rule.pattern.find('*') == std::string::npos && rule.regex.getloc() == locale) {
            // 大小写不同的规则可能对应同一节点，保留原顺序中的第一条。
            exactRules.try_emplace(exactKey(rule.pattern), i);
        } else {
            regexRules.push_back(i);
        }
    }
}

std::string PlayerPermissionRules::exactKey(std::string node) const {
    for (char& c : node) c = traits.translate_nocase(c);
    return node;
}

std::optional<bool> PlayerPermissionRules::match(const std::string& node) const {
    size_t firstExact = rules.size();
    if (!exactRules.empty()) {
        auto it = exactRules.find(exactKey(node));
        if (it != exactRules.end()) firstExact = it->second;
    }
    for (size_t i : regexRules) {
        if (i >= firstExact) break;
        if (std::regex_match(node, rules[i].regex)) return rules[i].state;
    }
    if (firstExact < rules.size()) return rules[firstExact].state;
    return std::nullopt;
}

uint64_t PermissionCache::playerPermissionsEpoch() const {
    std::shared_lock lock(m_playerPermissionsMutex);
    return m_playerPermsEpoch;
}

std::shared_ptr<const PlayerPermissionRules>
PermissionCache::findPlayerPermissions(const std::string& playerUuid) const {
    std::shared_lock lock(m_playerPermissionsMutex);
    auto             it = m_playerPermissionsCache.find(playerUuid);
    if (it != m_playerPermissionsCache.end()) {
        const auto& entry = it->second;
        if (!entry.earliestExpiry || currentUnixSeconds() < *entry.earliestExpiry) {
            return entry.rules;
        }
    }
    return nullptr;
}

void PermissionCache::storePlayerPermissions(
    const std::string&                           playerUuid,
    std::shared_ptr<const PlayerPermissionRules> permissions,
    std::optional<long long>                     earliestExpiry,
    std::vector<std::string>                     sourceGroups,
    uint64_t                                     expectedEpoch
) {
    std::unique_lock lock(m_playerPermissionsMutex);
    if (m_playerPermsEpoch != expectedEpoch) return;

    if (!m_playerPermissionsCache.contains(playerUuid) && m_playerPermissionsCache.size() >= kMaxPlayerCacheEntries) {
        const auto now = currentUnixSeconds();
        std::erase_if(m_playerPermissionsCache, [now](const auto& kv) {
            return kv.second.earliestExpiry && now >= *kv.second.earliestExpiry;
        });
        evictOldestToTarget(m_playerPermissionsCache, (kMaxPlayerCacheEntries * 3) / 4);
    }

    m_playerPermissionsCache.insert_or_assign(
        playerUuid,
        PlayerPermissionEntry{std::move(permissions), earliestExpiry, std::move(sourceGroups), ++m_playerPermsSeq}
    );
}

void PermissionCache::invalidatePlayerPermissions(const std::string& playerUuid) {
    std::unique_lock lock(m_playerPermissionsMutex);
    ++m_playerPermsEpoch;
    m_playerPermissionsCache.erase(playerUuid);
}

void PermissionCache::invalidatePlayerPermissionsForGroups(const std::set<std::string>& affectedGroups) {
    std::unique_lock lock(m_playerPermissionsMutex);
    ++m_playerPermsEpoch;
    if (affectedGroups.empty()) return;

    std::erase_if(m_playerPermissionsCache, [&affectedGroups](const auto& kv) {
        return std::any_of(
            kv.second.sourceGroups.begin(),
            kv.second.sourceGroups.end(),
            [&affectedGroups](const std::string& g) { return affectedGroups.contains(g); }
        );
    });
}

void PermissionCache::invalidateAllPlayerPermissions() {
    std::unique_lock lock(m_playerPermissionsMutex);
    ++m_playerPermsEpoch;
    m_playerPermissionsCache.clear();
}

// --- 玩家组派生缓存 ---

uint64_t PermissionCache::playerGroupsEpoch() const {
    std::shared_lock lock(m_playerGroupsMutex);
    return m_playerGroupsEpoch;
}

std::optional<std::vector<GroupDetails>> PermissionCache::findPlayerGroups(const std::string& playerUuid) const {
    std::shared_lock lock(m_playerGroupsMutex);
    auto             it = m_playerGroupsCache.find(playerUuid);
    if (it != m_playerGroupsCache.end()) {
        const auto                currentTime = currentUnixSeconds();
        std::vector<GroupDetails> validGroups;
        validGroups.reserve(it->second.groups.size());
        for (const auto& group : it->second.groups) {
            if (!group.expirationTime.has_value() || group.expirationTime.value() > currentTime) {
                validGroups.push_back(group);
            }
        }
        return validGroups;
    }
    return std::nullopt;
}

void PermissionCache::storePlayerGroups(
    const std::string&               playerUuid,
    const std::vector<GroupDetails>& groups,
    uint64_t                         expectedEpoch
) {
    std::unique_lock lock(m_playerGroupsMutex);
    if (m_playerGroupsEpoch != expectedEpoch) return;

    if (!m_playerGroupsCache.contains(playerUuid) && m_playerGroupsCache.size() >= kMaxPlayerCacheEntries) {
        const auto now = currentUnixSeconds();
        std::erase_if(m_playerGroupsCache, [now](const auto& kv) {
            const auto& gs = kv.second.groups;
            return !gs.empty() && std::all_of(gs.begin(), gs.end(), [now](const GroupDetails& g) {
                       return g.expirationTime.has_value() && *g.expirationTime <= now;
                   });
        });
        evictOldestToTarget(m_playerGroupsCache, (kMaxPlayerCacheEntries * 3) / 4);
    }

    m_playerGroupsCache.insert_or_assign(playerUuid, PlayerGroupEntry{groups, ++m_playerGroupsSeq});
}

void PermissionCache::invalidatePlayerGroups(const std::string& playerUuid) {
    std::unique_lock lock(m_playerGroupsMutex);
    ++m_playerGroupsEpoch;
    m_playerGroupsCache.erase(playerUuid);
}

void PermissionCache::invalidatePlayerGroupsForGroup(const std::string& groupName) {
    std::unique_lock lock(m_playerGroupsMutex);
    ++m_playerGroupsEpoch;

    std::erase_if(m_playerGroupsCache, [&groupName](const auto& kv) {
        return std::any_of(
            kv.second.groups.begin(),
            kv.second.groups.end(),
            [&groupName](const GroupDetails& g) { return g.name == groupName; }
        );
    });
}

void PermissionCache::invalidateAllPlayerGroups() {
    std::unique_lock lock(m_playerGroupsMutex);
    ++m_playerGroupsEpoch;
    m_playerGroupsCache.clear();
}

// --- 组权限派生缓存 ---

uint64_t PermissionCache::groupPermissionsEpoch() const {
    std::shared_lock lock(m_groupPermissionsMutex);
    return m_groupPermsEpoch;
}

std::optional<std::vector<CompiledPermissionRule>>
PermissionCache::findGroupPermissions(const std::string& groupName) const {
    std::shared_lock lock(m_groupPermissionsMutex);
    auto             it = m_groupPermissionsCache.find(groupName);
    if (it != m_groupPermissionsCache.end()) {
        return it->second.rules;
    }
    return std::nullopt;
}

void PermissionCache::storeGroupPermissions(
    const std::string&                         groupName,
    const std::vector<CompiledPermissionRule>& permissions,
    uint64_t                                   expectedEpoch
) {
    std::unique_lock lock(m_groupPermissionsMutex);
    if (m_groupPermsEpoch != expectedEpoch) return;

    if (!m_groupPermissionsCache.contains(groupName) && m_groupPermissionsCache.size() >= kMaxGroupPermCacheEntries) {
        evictOldestToTarget(m_groupPermissionsCache, (kMaxGroupPermCacheEntries * 3) / 4);
    }

    m_groupPermissionsCache.insert_or_assign(groupName, GroupPermissionEntry{permissions, ++m_groupPermsSeq});
}

void PermissionCache::invalidateGroupPermissions(const std::string& groupName) {
    std::unique_lock lock(m_groupPermissionsMutex);
    ++m_groupPermsEpoch;
    m_groupPermissionsCache.erase(groupName);
}

void PermissionCache::invalidateGroupPermissionsForGroups(const std::set<std::string>& groupNames) {
    std::unique_lock lock(m_groupPermissionsMutex);
    ++m_groupPermsEpoch;
    for (const auto& name : groupNames) {
        m_groupPermissionsCache.erase(name);
    }
}

void PermissionCache::invalidateAllGroupPermissions() {
    std::unique_lock lock(m_groupPermissionsMutex);
    ++m_groupPermsEpoch;
    m_groupPermissionsCache.clear();
}

// --- 默认权限权威索引 ---

bool PermissionCache::arePermissionDefaultsLoaded() const {
    std::shared_lock lock(m_permissionDefaultsMutex);
    return m_permissionDefaultsLoaded;
}

std::optional<bool> PermissionCache::findPermissionDefault(const std::string& permissionName) const {
    std::shared_lock lock(m_permissionDefaultsMutex);
    auto             it = m_permissionDefaultsCache.find(permissionName);
    if (it != m_permissionDefaultsCache.end()) {
        return it->second;
    }
    return std::nullopt;
}

void PermissionCache::storePermissionDefault(const std::string& permissionName, bool defaultValue) {
    std::unique_lock lock(m_permissionDefaultsMutex);
    m_permissionDefaultsCache[permissionName] = defaultValue;
}

void PermissionCache::populateAllPermissionDefaults(std::unordered_map<std::string, bool>&& defaultsMap) {
    std::unique_lock lock(m_permissionDefaultsMutex);
    m_permissionDefaultsCache  = std::move(defaultsMap);
    m_permissionDefaultsLoaded = true;
}

std::unordered_map<std::string, bool> PermissionCache::getAllPermissionDefaults() const {
    std::shared_lock lock(m_permissionDefaultsMutex);
    return m_permissionDefaultsCache;
}

// --- 继承图权威索引 ---

void PermissionCache::populateInheritance(
    std::unordered_map<std::string, std::set<std::string>>&& parentToChildren,
    std::unordered_map<std::string, std::set<std::string>>&& childToParents
) {
    std::unique_lock lock(m_inheritanceMutex);
    m_parentToChildren = std::move(parentToChildren);
    m_childToParents   = std::move(childToParents);
}

void PermissionCache::addInheritance(const std::string& child, const std::string& parent) {
    std::unique_lock lock(m_inheritanceMutex);
    m_childToParents[child].insert(parent);
    m_parentToChildren[parent].insert(child);
}

void PermissionCache::removeInheritance(const std::string& child, const std::string& parent) {
    std::unique_lock lock(m_inheritanceMutex);
    if (auto it = m_childToParents.find(child); it != m_childToParents.end()) {
        it->second.erase(parent);
        if (it->second.empty()) {
            m_childToParents.erase(it);
        }
    }
    if (auto it = m_parentToChildren.find(parent); it != m_parentToChildren.end()) {
        it->second.erase(child);
        if (it->second.empty()) {
            m_parentToChildren.erase(it);
        }
    }
}

void PermissionCache::removeGroupFromInheritance(const std::string& groupName) {
    std::unique_lock lock(m_inheritanceMutex);
    auto removeEdges = [&](auto& adjacent, auto& reverse) {
        auto node = adjacent.find(groupName);
        if (node == adjacent.end()) return;
        for (const auto& neighbor : node->second) {
            if (auto rev = reverse.find(neighbor); rev != reverse.end()) {
                rev->second.erase(groupName);
                if (rev->second.empty()) {
                    reverse.erase(rev);
                }
            }
        }
        adjacent.erase(node);
    };
    removeEdges(m_childToParents, m_parentToChildren);
    removeEdges(m_parentToChildren, m_childToParents);
}

bool PermissionCache::canAddInheritance(const std::string& child, const std::string& parent) const {
    if (child == parent) return false;
    std::shared_lock lock(m_inheritanceMutex);
    if (auto parents = m_childToParents.find(child);
        parents != m_childToParents.end() && parents->second.contains(parent)) {
        return false;
    }

    std::queue<std::string> q;
    std::set<std::string>   visited;

    // 新边为 parent -> child，只有 child 已能到达 parent 时才会成环。
    q.push(child);
    visited.insert(child);

    while (!q.empty()) {
        std::string current = q.front();
        q.pop();

        auto it = m_parentToChildren.find(current);
        if (it != m_parentToChildren.end()) {
            for (const auto& neighbor : it->second) {
                if (neighbor == parent) return false;
                if (visited.insert(neighbor).second) {
                    q.push(neighbor);
                }
            }
        }
    }
    return true;
}

std::vector<std::string> PermissionCache::getDirectParentGroups(const std::string& groupName) const {
    std::shared_lock lock(m_inheritanceMutex);
    auto             it = m_childToParents.find(groupName);
    if (it == m_childToParents.end()) {
        return {};
    }
    return std::vector<std::string>(it->second.begin(), it->second.end());
}

std::set<std::string> PermissionCache::getAllAncestorGroups(const std::string& groupName) const {
    std::shared_lock        lock(m_inheritanceMutex);
    std::set<std::string>   ancestors;
    std::queue<std::string> q;

    q.push(groupName);
    ancestors.insert(groupName);

    while (!q.empty()) {
        std::string current = q.front();
        q.pop();
        auto it = m_childToParents.find(current);
        if (it != m_childToParents.end()) {
            for (const auto& parent : it->second) {
                if (ancestors.insert(parent).second) {
                    q.push(parent);
                }
            }
        }
    }
    return ancestors;
}

std::set<std::string> PermissionCache::getChildGroupsRecursive(const std::string& groupName) const {
    std::shared_lock        lock(m_inheritanceMutex);
    std::set<std::string>   children;
    std::queue<std::string> q;

    q.push(groupName);
    children.insert(groupName);

    while (!q.empty()) {
        std::string current = q.front();
        q.pop();
        auto it = m_parentToChildren.find(current);
        if (it != m_parentToChildren.end()) {
            for (const auto& child : it->second) {
                if (children.insert(child).second) {
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
