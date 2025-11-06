
#include "permission/PermissionCache.h" 
#include <queue>                        
#include <chrono>                       

namespace BA {
namespace permission {
namespace internal {

using namespace std; // 使用标准命名空间

// --- 组名/ID 缓存 ---

// 查找组ID
// 参数: groupName - 组名
// 返回: 组ID (如果找到)
optional<string> PermissionCache::findGroupId(const string& groupName) {
    shared_lock<shared_mutex> lock(m_groupNameMutex); // 获取读锁
    auto                      it = m_groupNameCache.find(groupName); // 在组名缓存中查找
    if (it != m_groupNameCache.end()) {
        return it->second; // 返回找到的组ID
    }
    return nullopt; // 未找到则返回空
}

// 查找组名
// 参数: groupId - 组ID
// 返回: 组名 (如果找到)
optional<string> PermissionCache::findGroupName(const string& groupId) {
    shared_lock<shared_mutex> lock(m_groupIdMutex); // 获取读锁
    auto                      it = m_groupIdCache.find(groupId); // 在组ID缓存中查找
    if (it != m_groupIdCache.end()) {
        return it->second; // 返回找到的组名
    }
    return nullopt; // 未找到则返回空
}

// 存储组名和组ID
// 参数: groupName - 组名, groupId - 组ID
void PermissionCache::storeGroup(const string& groupName, const string& groupId) {
    unique_lock<shared_mutex> nameLock(m_groupNameMutex); // 获取组名缓存的写锁
    unique_lock<shared_mutex> idLock(m_groupIdMutex);     // 获取组ID缓存的写锁

    // 如果旧的组名存在，先移除旧的ID映射
    auto it = m_groupNameCache.find(groupName);
    if (it != m_groupNameCache.end()) {
        m_groupIdCache.erase(it->second); // 移除旧的ID到名称的映射
    }

    m_groupNameCache[groupName] = groupId; // 存储组名到组ID的映射
    m_groupIdCache[groupId]     = groupName; // 存储组ID到组名的映射
}

// 使组缓存失效
// 参数: groupName - 组名
void PermissionCache::invalidateGroup(const string& groupName) {
    unique_lock<shared_mutex> nameLock(m_groupNameMutex); // 获取组名缓存的写锁
    unique_lock<shared_mutex> idLock(m_groupIdMutex);     // 获取组ID缓存的写锁

    auto it = m_groupNameCache.find(groupName);
    if (it != m_groupNameCache.end()) {
        m_groupIdCache.erase(it->second); // 移除ID到名称的映射
        m_groupNameCache.erase(it);       // 移除名称到ID的映射
    }
}

// 批量填充所有组
// 参数: groupNameMap - 组名到组ID的映射
void PermissionCache::populateAllGroups(unordered_map<string, string>&& groupNameMap) {
    unique_lock<shared_mutex> nameLock(m_groupNameMutex); // 获取组名缓存的写锁
    unique_lock<shared_mutex> idLock(m_groupIdMutex);     // 获取组ID缓存的写锁

    m_groupNameCache.clear(); // 清空组名缓存
    m_groupIdCache.clear();   // 清空组ID缓存

    for (auto& pair : groupNameMap) {
        m_groupNameCache[pair.first] = pair.second; // 填充组名到组ID的映射
        m_groupIdCache[pair.second]  = pair.first;  // 填充组ID到组名的映射
    }
}

// 获取所有组
// 返回: 所有组的映射关系
const std::unordered_map<std::string, std::string>& PermissionCache::getAllGroups() const {
    shared_lock<shared_mutex> lock(m_groupNameMutex); // 获取读锁
    return m_groupNameCache; // 返回组名缓存
}

// --- 玩家权限缓存 ---

// 查找玩家权限
// 参数: playerUuid - 玩家UUID
// 返回: 编译后的权限规则列表 (如果找到)
optional<vector<CompiledPermissionRule>> PermissionCache::findPlayerPermissions(const string& playerUuid) {
    shared_lock<shared_mutex> lock(m_playerPermissionsMutex); // 获取读锁
    auto                      it = m_playerPermissionsCache.find(playerUuid); // 在玩家权限缓存中查找
    if (it != m_playerPermissionsCache.end()) {
        return it->second; // 返回找到的权限规则
    }
    return nullopt; // 未找到则返回空
}

// 存储玩家权限
// 参数: playerUuid - 玩家UUID, permissions - 权限规则列表
void PermissionCache::storePlayerPermissions(
    const string&                         playerUuid,
    const vector<CompiledPermissionRule>& permissions
) {
    unique_lock<shared_mutex> lock(m_playerPermissionsMutex); // 获取写锁
    m_playerPermissionsCache[playerUuid] = permissions; // 存储玩家权限
}

// 使玩家权限缓存失效
// 参数: playerUuid - 玩家UUID
void PermissionCache::invalidatePlayerPermissions(const string& playerUuid) {
    unique_lock<shared_mutex> lock(m_playerPermissionsMutex); // 获取写锁
    m_playerPermissionsCache.erase(playerUuid); // 移除玩家权限
}

// 使所有玩家权限缓存失效
void PermissionCache::invalidateAllPlayerPermissions() {
    unique_lock<shared_mutex> lock(m_playerPermissionsMutex); // 获取写锁
    m_playerPermissionsCache.clear(); // 清空玩家权限缓存
}

// --- 玩家组缓存 ---

// 查找玩家组
// 参数: playerUuid - 玩家UUID
// 返回: 组详情列表 (如果找到)
optional<vector<GroupDetails>> PermissionCache::findPlayerGroups(const string& playerUuid) {
    shared_lock<shared_mutex> lock(m_playerGroupsMutex); // 获取读锁
    auto                      it = m_playerGroupsCache.find(playerUuid); // 在玩家组缓存中查找
    if (it != m_playerGroupsCache.end()) {
        // 缓存命中，现在检查条目是否过期
        long long              currentTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        vector<GroupDetails>   validGroups;
        for (const auto& group : it->second) {
            if (!group.expirationTime.has_value() || group.expirationTime.value() > currentTime) {
                validGroups.push_back(group);
            }
        }

        if (!validGroups.empty()) {
            return validGroups; // 返回未过期的组
        }
        // 如果所有组都已过期，则视为缓存未命中
    }
    return nullopt; // 未找到或所有组都已过期
}

// 存储玩家组
// 参数: playerUuid - 玩家UUID, groups - 组详情列表
void PermissionCache::storePlayerGroups(const string& playerUuid, const vector<GroupDetails>& groups) {
    unique_lock<shared_mutex> lock(m_playerGroupsMutex); // 获取写锁
    m_playerGroupsCache[playerUuid] = groups; // 存储玩家组
}

// 使玩家组缓存失效
// 参数: playerUuid - 玩家UUID
void PermissionCache::invalidatePlayerGroups(const string& playerUuid) {
    unique_lock<shared_mutex> lock(m_playerGroupsMutex); // 获取写锁
    m_playerGroupsCache.erase(playerUuid); // 移除玩家组
}

// --- 组权限缓存 ---

// 查找组权限
// 参数: groupName - 组名
// 返回: 编译后的权限规则列表 (如果找到)
optional<vector<CompiledPermissionRule>> PermissionCache::findGroupPermissions(const string& groupName) {
    shared_lock<shared_mutex> lock(m_groupPermissionsMutex); // 获取读锁
    auto                      it = m_groupPermissionsCache.find(groupName); // 在组权限缓存中查找
    if (it != m_groupPermissionsCache.end()) {
        return it->second; // 返回找到的权限规则
    }
    return nullopt; // 未找到则返回空
}

// 存储组权限
// 参数: groupName - 组名, permissions - 权限规则列表
void PermissionCache::storeGroupPermissions(
    const string&                         groupName,
    const vector<CompiledPermissionRule>& permissions
) {
    unique_lock<shared_mutex> lock(m_groupPermissionsMutex); // 获取写锁
    m_groupPermissionsCache[groupName] = permissions; // 存储组权限
}

// 使组权限缓存失效
// 参数: groupName - 组名
void PermissionCache::invalidateGroupPermissions(const string& groupName) {
    unique_lock<shared_mutex> lock(m_groupPermissionsMutex); // 获取写锁
    m_groupPermissionsCache.erase(groupName); // 移除组权限
}

// 使所有组权限缓存失效
void PermissionCache::invalidateAllGroupPermissions() {
    unique_lock<shared_mutex> lock(m_groupPermissionsMutex); // 获取写锁
    m_groupPermissionsCache.clear(); // 清空组权限缓存
}

// --- 默认权限缓存 ---

// 查找默认权限
// 参数: permissionName - 权限名
// 返回: 默认值 (如果找到)
optional<bool> PermissionCache::findPermissionDefault(const string& permissionName) {
    shared_lock<shared_mutex> lock(m_permissionDefaultsMutex); // 获取读锁
    auto                      it = m_permissionDefaultsCache.find(permissionName); // 在默认权限缓存中查找
    if (it != m_permissionDefaultsCache.end()) {
        return it->second; // 返回找到的默认值
    }
    return nullopt; // 未找到则返回空
}

// 存储默认权限
// 参数: permissionName - 权限名, defaultValue - 默认值
void PermissionCache::storePermissionDefault(const string& permissionName, bool defaultValue) {
    unique_lock<shared_mutex> lock(m_permissionDefaultsMutex); // 获取写锁
    m_permissionDefaultsCache[permissionName] = defaultValue; // 存储默认权限
}

// 批量填充所有默认权限
// 参数: defaultsMap - 默认权限映射
void PermissionCache::populateAllPermissionDefaults(unordered_map<string, bool>&& defaultsMap) {
    unique_lock<shared_mutex> lock(m_permissionDefaultsMutex); // 获取写锁
    m_permissionDefaultsCache = std::move(defaultsMap); // 移动赋值填充默认权限缓存
}

// 获取所有默认权限
// 返回: 所有默认权限的映射关系
const std::unordered_map<std::string, bool>& PermissionCache::getAllPermissionDefaults() const {
    shared_lock<shared_mutex> lock(m_permissionDefaultsMutex); // 获取读锁
    return m_permissionDefaultsCache; // 返回默认权限缓存
}

// --- 继承缓存 ---

// 填充继承关系
// 参数: parentToChildren - 父组到子组的映射, childToParents - 子组到父组的映射
void PermissionCache::populateInheritance(
    unordered_map<string, set<string>>&& parentToChildren,
    unordered_map<string, set<string>>&& childToParents
) {
    unique_lock<shared_mutex> lock(m_inheritanceMutex); // 获取写锁
    m_parentToChildren = std::move(parentToChildren); // 移动赋值父组到子组的映射
    m_childToParents   = std::move(childToParents);   // 移动赋值子组到父组的映射
}

// 添加继承关系
// 参数: child - 子组, parent - 父组
void PermissionCache::addInheritance(const string& child, const string& parent) {
    unique_lock<shared_mutex> lock(m_inheritanceMutex); // 获取写锁
    m_childToParents[child].insert(parent); // 在子组到父组的映射中添加关系
    m_parentToChildren[parent].insert(child); // 在父组到子组的映射中添加关系
}

// 移除继承关系
// 参数: child - 子组, parent - 父组
void PermissionCache::removeInheritance(const string& child, const string& parent) {
    unique_lock<shared_mutex> lock(m_inheritanceMutex); // 获取写锁
    if (m_childToParents.count(child)) { // 如果子组存在
        m_childToParents[child].erase(parent); // 移除子组到父组的映射
        if (m_childToParents[child].empty()) { // 如果子组的父组列表为空
            m_childToParents.erase(child); // 移除该子组
        }
    }
    if (m_parentToChildren.count(parent)) { // 如果父组存在
        m_parentToChildren[parent].erase(child); // 移除父组到子组的映射
        if (m_parentToChildren[parent].empty()) { // 如果父组的子组列表为空
            m_parentToChildren.erase(parent); // 移除该父组
        }
    }
}

// 检查是否存在路径
// 参数: startNode - 起始节点, endNode - 结束节点
// 返回: 如果存在路径则为true，否则为false
bool PermissionCache::hasPath(const string& startNode, const string& endNode) const {
    if (startNode == endNode) return true; // 如果起始节点和结束节点相同，则存在路径
    shared_lock<shared_mutex> lock(m_inheritanceMutex); // 获取读锁

    queue<string> q;     // 广度优先搜索队列
    set<string>   visited; // 已访问节点集合

    q.push(startNode);     // 将起始节点加入队列
    visited.insert(startNode); // 将起始节点标记为已访问

    while (!q.empty()) {
        string current = q.front(); // 获取当前节点
        q.pop();                   // 移除当前节点

        auto it = m_parentToChildren.find(current); // 查找当前节点的子节点
        if (it != m_parentToChildren.end()) {
            for (const auto& neighbor : it->second) { // 遍历所有子节点
                if (neighbor == endNode) return true; // 如果找到结束节点，则存在路径
                if (visited.find(neighbor) == visited.end()) { // 如果子节点未被访问
                    visited.insert(neighbor); // 标记为已访问
                    q.push(neighbor);         // 加入队列
                }
            }
        }
    }
    return false; // 未找到路径
}

// 获取所有祖先组
// 参数: groupName - 组名
// 返回: 所有祖先组的集合 (包括自身)
set<string> PermissionCache::getAllAncestorGroups(const string& groupName) const {
    shared_lock<shared_mutex> lock(m_inheritanceMutex); // 获取读锁
    set<string>               ancestors; // 祖先组集合
    queue<string>             q;         // 广度优先搜索队列

    q.push(groupName);     // 将当前组加入队列
    ancestors.insert(groupName); // 将当前组标记为祖先

    while (!q.empty()) {
        string current = q.front(); // 获取当前节点
        q.pop();                   // 移除当前节点
        auto it = m_childToParents.find(current); // 查找当前节点的父节点
        if (it != m_childToParents.end()) {
            for (const auto& parent : it->second) { // 遍历所有父节点
                if (ancestors.find(parent) == ancestors.end()) { // 如果父节点未被访问
                    ancestors.insert(parent); // 标记为祖先
                    q.push(parent);           // 加入队列
                }
            }
        }
    }
    return ancestors; // 返回所有祖先组
}

// 递归获取所有子组
// 参数: groupName - 组名
// 返回: 所有子组的集合 (包括自身)
set<string> PermissionCache::getChildGroupsRecursive(const string& groupName) const {
    shared_lock<shared_mutex> lock(m_inheritanceMutex); // 获取读锁
    set<string>               children; // 子组集合
    queue<string>             q;        // 广度优先搜索队列

    q.push(groupName);     // 将当前组加入队列
    children.insert(groupName); // 将当前组标记为子组

    while (!q.empty()) {
        string current = q.front(); // 获取当前节点
        q.pop();                   // 移除当前节点
        auto it = m_parentToChildren.find(current); // 查找当前节点的子节点
        if (it != m_parentToChildren.end()) {
            for (const auto& child : it->second) { // 遍历所有子节点
                if (children.find(child) == children.end()) { // 如果子节点未被访问
                    children.insert(child); // 标记为子组
                    q.push(child);          // 加入队列
                }
            }
        }
    }
    return children; // 返回所有子组
}


} // namespace internal
} // namespace permission
} // namespace BA
