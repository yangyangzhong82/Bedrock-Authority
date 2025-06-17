#include "permission/PermissionManager.h"
#include "permission/PermissionManagerImpl.h" // 包含实现类的头文件

namespace BA {
namespace permission {

// --- 单例和生命周期 ---

/**
 * @brief 获取 PermissionManager 的单例实例。
 * @return PermissionManager 的单例引用。
 */
PermissionManager& PermissionManager::getInstance() {
    static PermissionManager instance; // 静态局部变量，保证只创建一次实例
    return instance;
}

/**
 * @brief PermissionManager 构造函数。
 *        现在只负责创建实现对象 (PIMPL)。
 */
PermissionManager::PermissionManager() : m_pimpl(std::make_unique<PermissionManagerImpl>()) {}

/**
 * @brief PermissionManager 析构函数。
 *        对于使用不完整类型 (incomplete type) 的 std::unique_ptr，这里需要定义析构函数。
 */
PermissionManager::~PermissionManager() = default;

/**
 * @brief 初始化权限管理器。
 * @param db 数据库接口指针。
 * @param enableWarmup 是否启用预热缓存。
 * @param threadPoolSize 线程池大小。
 * @return 如果初始化成功则返回 true，否则返回 false。
 */
bool PermissionManager::init(db::IDatabase* db, bool enableWarmup, unsigned int threadPoolSize) {
    return m_pimpl->init(db, enableWarmup, threadPoolSize);
}

/**
 * @brief 关闭权限管理器。
 */
void PermissionManager::shutdown() { m_pimpl->shutdown(); }

// --- 所有其他方法都只是转发调用到实现类 ---

/**
 * @brief 注册一个权限。
 * @param name 权限名称。
 * @param description 权限描述。
 * @param defaultValue 权限的默认值。
 * @return 如果注册成功则返回 true，否则返回 false。
 */
bool PermissionManager::registerPermission(const std::string& name, const std::string& description, bool defaultValue) {
    return m_pimpl->registerPermission(name, description, defaultValue);
}

/**
 * @brief 检查权限是否存在。
 * @param name 权限名称。
 * @return 如果权限存在则返回 true，否则返回 false。
 */
bool PermissionManager::permissionExists(const std::string& name) { return m_pimpl->permissionExists(name); }

/**
 * @brief 获取所有已注册的权限。
 * @return 包含所有权限名称的字符串向量。
 */
std::vector<std::string> PermissionManager::getAllPermissions() { return m_pimpl->getAllPermissions(); }

/**
 * @brief 创建一个权限组。
 * @param groupName 组名称。
 * @param description 组描述。
 * @return 如果创建成功则返回 true，否则返回 false。
 */
bool PermissionManager::createGroup(const std::string& groupName, const std::string& description) {
    return m_pimpl->createGroup(groupName, description);
}

/**
 * @brief 删除一个权限组。
 * @param groupName 组名称。
 * @return 如果删除成功则返回 true，否则返回 false。
 */
bool PermissionManager::deleteGroup(const std::string& groupName) { return m_pimpl->deleteGroup(groupName); }

/**
 * @brief 检查权限组是否存在。
 * @param groupName 组名称。
 * @return 如果组存在则返回 true，否则返回 false。
 */
bool PermissionManager::groupExists(const std::string& groupName) { return m_pimpl->groupExists(groupName); }

/**
 * @brief 获取所有权限组。
 * @return 包含所有组名称的字符串向量。
 */
std::vector<std::string> PermissionManager::getAllGroups() { return m_pimpl->getAllGroups(); }

/**
 * @brief 获取权限组的详细信息。
 * @param groupName 组名称。
 * @return 组的详细信息结构体。
 */
GroupDetails PermissionManager::getGroupDetails(const std::string& groupName) {
    return m_pimpl->getGroupDetails(groupName);
}

/**
 * @brief 更新权限组的描述。
 * @param groupName 组名称。
 * @param newDescription 新的组描述。
 * @return 如果更新成功则返回 true，否则返回 false。
 */
bool PermissionManager::updateGroupDescription(const std::string& groupName, const std::string& newDescription) {
    return m_pimpl->updateGroupDescription(groupName, newDescription);
}

/**
 * @brief 获取权限组的描述。
 * @param groupName 组名称。
 * @return 组的描述字符串。
 */
std::string PermissionManager::getGroupDescription(const std::string& groupName) {
    return m_pimpl->getGroupDescription(groupName);
}

/**
 * @brief 向权限组添加一个权限。
 * @param groupName 组名称。
 * @param permissionName 权限名称。
 * @return 如果添加成功则返回 true，否则返回 false。
 */
bool PermissionManager::addPermissionToGroup(const std::string& groupName, const std::string& permissionName) {
    return m_pimpl->addPermissionToGroup(groupName, permissionName);
}

/**
 * @brief 从权限组中移除一个权限。
 * @param groupName 组名称。
 * @param permissionName 权限名称。
 * @return 如果移除成功则返回 true，否则返回 false。
 */
bool PermissionManager::removePermissionFromGroup(const std::string& groupName, const std::string& permissionName) {
    return m_pimpl->removePermissionFromGroup(groupName, permissionName);
}

/**
 * @brief 获取权限组直接拥有的权限。
 * @param groupName 组名称。
 * @return 包含直接权限名称的字符串向量。
 */
std::vector<std::string> PermissionManager::getDirectPermissionsOfGroup(const std::string& groupName) {
    return m_pimpl->getDirectPermissionsOfGroup(groupName);
}

/**
 * @brief 获取权限组的所有编译后的权限规则（包括继承的）。
 * @param groupName 组名称。
 * @return 包含编译后权限规则的向量。
 */
std::vector<CompiledPermissionRule> PermissionManager::getPermissionsOfGroup(const std::string& groupName) {
    return m_pimpl->getPermissionsOfGroup(groupName);
}

/**
 * @brief 向权限组批量添加权限规则。
 * @param groupName 组名称。
 * @param permissionRules 权限规则字符串向量。
 * @return 成功添加的权限规则数量。
 */
size_t PermissionManager::addPermissionsToGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    return m_pimpl->addPermissionsToGroup(groupName, permissionRules);
}

/**
 * @brief 从权限组批量移除权限规则。
 * @param groupName 组名称。
 * @param permissionRules 权限规则字符串向量。
 * @return 成功移除的权限规则数量。
 */
size_t PermissionManager::removePermissionsFromGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    return m_pimpl->removePermissionsFromGroup(groupName, permissionRules);
}

/**
 * @brief 添加组继承关系。
 * @param groupName 子组名称。
 * @param parentGroupName 父组名称。
 * @return 如果添加成功则返回 true，否则返回 false。
 */
bool PermissionManager::addGroupInheritance(const std::string& groupName, const std::string& parentGroupName) {
    return m_pimpl->addGroupInheritance(groupName, parentGroupName);
}

/**
 * @brief 移除组继承关系。
 * @param groupName 子组名称。
 * @param parentGroupName 父组名称。
 * @return 如果移除成功则返回 true，否则返回 false。
 */
bool PermissionManager::removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName) {
    return m_pimpl->removeGroupInheritance(groupName, parentGroupName);
}

/**
 * @brief 获取权限组的所有祖先组（包括间接继承的）。
 * @param groupName 组名称。
 * @return 包含所有祖先组名称的字符串向量。
 */
std::vector<std::string> PermissionManager::getAllAncestorGroups(const std::string& groupName) {
    return m_pimpl->getAllAncestorGroups(groupName);
}

/**
 * @brief 获取权限组的直接父组。
 * @param groupName 组名称。
 * @return 包含直接父组名称的字符串向量。
 */
std::vector<std::string> PermissionManager::getDirectParentGroups(const std::string& groupName) {
    return m_pimpl->getDirectParentGroups(groupName);
}

/**
 * @brief 设置权限组的优先级。
 * @param groupName 组名称。
 * @param priority 优先级值。
 * @return 如果设置成功则返回 true，否则返回 false。
 */
bool PermissionManager::setGroupPriority(const std::string& groupName, int priority) {
    return m_pimpl->setGroupPriority(groupName, priority);
}

/**
 * @brief 获取权限组的优先级。
 * @param groupName 组名称。
 * @return 组的优先级值。
 */
int PermissionManager::getGroupPriority(const std::string& groupName) { return m_pimpl->getGroupPriority(groupName); }

/**
 * @brief 将玩家添加到权限组。
 * @param playerUuid 玩家的 UUID。
 * @param groupName 组名称。
 * @return 如果添加成功则返回 true，否则返回 false。
 */
bool PermissionManager::addPlayerToGroup(const std::string& playerUuid, const std::string& groupName) {
    return m_pimpl->addPlayerToGroup(playerUuid, groupName);
}
bool PermissionManager::addPlayerToGroup(
    const std::string& playerUuid,
    const std::string& groupName,
    long long          durationSeconds
) {
    return m_pimpl->addPlayerToGroup(playerUuid, groupName, durationSeconds);
}

/**
 * @brief 将玩家从权限组中移除。
 * @param playerUuid 玩家的 UUID。
 * @param groupName 组名称。
 * @return 如果移除成功则返回 true，否则返回 false。
 */
bool PermissionManager::removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName) {
    return m_pimpl->removePlayerFromGroup(playerUuid, groupName);
}

/**
 * @brief 获取玩家所属的所有权限组。
 * @param playerUuid 玩家的 UUID。
 * @return 包含玩家所属组名称的字符串向量。
 */
std::vector<std::string> PermissionManager::getPlayerGroups(const std::string& playerUuid) {
    return m_pimpl->getPlayerGroups(playerUuid);
}

/**
 * @brief 获取玩家所属的所有权限组 ID。
 * @param playerUuid 玩家的 UUID。
 * @return 包含玩家所属组 ID 的字符串向量。
 */
std::vector<std::string> PermissionManager::getPlayerGroupIds(const std::string& playerUuid) {
    return m_pimpl->getPlayerGroupIds(playerUuid);
}

/**
 * @brief 获取指定权限组中的所有玩家 UUID。
 * @param groupName 组名称。
 * @return 包含组中所有玩家 UUID 的字符串向量。
 */
std::vector<std::string> PermissionManager::getPlayersInGroup(const std::string& groupName) {
    return m_pimpl->getPlayersInGroup(groupName);
}

/**
 * @brief 获取玩家所属的所有权限组及其优先级。
 * @param playerUuid 玩家的 UUID。
 * @return 包含玩家所属组详细信息（包括优先级）的向量。
 */
std::vector<GroupDetails> PermissionManager::getPlayerGroupsWithPriorities(const std::string& playerUuid) {
    return m_pimpl->getPlayerGroupsWithPriorities(playerUuid);
}

/**
 * @brief 将玩家批量添加到权限组。
 * @param playerUuid 玩家的 UUID。
 * @param groupNames 组名称字符串向量。
 * @return 成功添加的组数量。
 */
size_t PermissionManager::addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames) {
    return m_pimpl->addPlayerToGroups(playerUuid, groupNames);
}

/**
 * @brief 将玩家从权限组中批量移除。
 * @param playerUuid 玩家的 UUID。
 * @param groupNames 组名称字符串向量。
 * @return 成功移除的组数量。
 */
size_t
PermissionManager::removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames) {
    return m_pimpl->removePlayerFromGroups(playerUuid, groupNames);
}

/**
 * @brief 获取玩家的所有编译后的权限规则（包括继承的）。
 * @param playerUuid 玩家的 UUID。
 * @return 包含编译后权限规则的向量。
 */
std::vector<CompiledPermissionRule> PermissionManager::getAllPermissionsForPlayer(const std::string& playerUuid) {
    return m_pimpl->getAllPermissionsForPlayer(playerUuid);
}

/**
 * @brief 检查玩家是否拥有某个权限。
 * @param playerUuid 玩家的 UUID。
 * @param permissionNode 权限节点。
 * @return 如果玩家拥有该权限则返回 true，否则返回 false。
 */
bool PermissionManager::hasPermission(const std::string& playerUuid, const std::string& permissionNode) {
    return m_pimpl->hasPermission(playerUuid, permissionNode);
}
void PermissionManager::runPeriodicCleanup() { m_pimpl->runPeriodicCleanup(); }

} // namespace permission
} // namespace BA
