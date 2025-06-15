#include "permission/PermissionManager.h"
#include "permission/PermissionManagerImpl.h" // Include the implementation header

namespace BA {
namespace permission {

// --- Singleton and Lifecycle ---

PermissionManager& PermissionManager::getInstance() {
    static PermissionManager instance;
    return instance;
}

// The constructor now simply creates the implementation object.
PermissionManager::PermissionManager() : m_pimpl(std::make_unique<PermissionManagerImpl>()) {}

// The destructor is required here for std::unique_ptr with an incomplete type.
PermissionManager::~PermissionManager() = default;

bool PermissionManager::init(db::IDatabase* db, bool enableWarmup, unsigned int threadPoolSize) {
    return m_pimpl->init(db, enableWarmup, threadPoolSize);
}

void PermissionManager::shutdown() { m_pimpl->shutdown(); }

// --- All other methods are just forwarding calls to the implementation ---

bool PermissionManager::registerPermission(const std::string& name, const std::string& description, bool defaultValue) {
    return m_pimpl->registerPermission(name, description, defaultValue);
}

bool PermissionManager::permissionExists(const std::string& name) { return m_pimpl->permissionExists(name); }

std::vector<std::string> PermissionManager::getAllPermissions() { return m_pimpl->getAllPermissions(); }

bool PermissionManager::createGroup(const std::string& groupName, const std::string& description) {
    return m_pimpl->createGroup(groupName, description);
}

bool PermissionManager::deleteGroup(const std::string& groupName) { return m_pimpl->deleteGroup(groupName); }

bool PermissionManager::groupExists(const std::string& groupName) { return m_pimpl->groupExists(groupName); }

std::vector<std::string> PermissionManager::getAllGroups() { return m_pimpl->getAllGroups(); }

GroupDetails PermissionManager::getGroupDetails(const std::string& groupName) {
    return m_pimpl->getGroupDetails(groupName);
}

bool PermissionManager::updateGroupDescription(const std::string& groupName, const std::string& newDescription) {
    return m_pimpl->updateGroupDescription(groupName, newDescription);
}

std::string PermissionManager::getGroupDescription(const std::string& groupName) {
    return m_pimpl->getGroupDescription(groupName);
}

bool PermissionManager::addPermissionToGroup(const std::string& groupName, const std::string& permissionName) {
    return m_pimpl->addPermissionToGroup(groupName, permissionName);
}

bool PermissionManager::removePermissionFromGroup(const std::string& groupName, const std::string& permissionName) {
    return m_pimpl->removePermissionFromGroup(groupName, permissionName);
}

std::vector<std::string> PermissionManager::getDirectPermissionsOfGroup(const std::string& groupName) {
    return m_pimpl->getDirectPermissionsOfGroup(groupName);
}

std::vector<CompiledPermissionRule> PermissionManager::getPermissionsOfGroup(const std::string& groupName) {
    return m_pimpl->getPermissionsOfGroup(groupName);
}

size_t PermissionManager::addPermissionsToGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    return m_pimpl->addPermissionsToGroup(groupName, permissionRules);
}

size_t PermissionManager::removePermissionsFromGroup(
    const std::string&              groupName,
    const std::vector<std::string>& permissionRules
) {
    return m_pimpl->removePermissionsFromGroup(groupName, permissionRules);
}

bool PermissionManager::addGroupInheritance(const std::string& groupName, const std::string& parentGroupName) {
    return m_pimpl->addGroupInheritance(groupName, parentGroupName);
}

bool PermissionManager::removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName) {
    return m_pimpl->removeGroupInheritance(groupName, parentGroupName);
}

std::vector<std::string> PermissionManager::getParentGroups(const std::string& groupName) {
    return m_pimpl->getParentGroups(groupName);
}

bool PermissionManager::setGroupPriority(const std::string& groupName, int priority) {
    return m_pimpl->setGroupPriority(groupName, priority);
}

int PermissionManager::getGroupPriority(const std::string& groupName) { return m_pimpl->getGroupPriority(groupName); }

bool PermissionManager::addPlayerToGroup(const std::string& playerUuid, const std::string& groupName) {
    return m_pimpl->addPlayerToGroup(playerUuid, groupName);
}

bool PermissionManager::removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName) {
    return m_pimpl->removePlayerFromGroup(playerUuid, groupName);
}

std::vector<std::string> PermissionManager::getPlayerGroups(const std::string& playerUuid) {
    return m_pimpl->getPlayerGroups(playerUuid);
}

std::vector<std::string> PermissionManager::getPlayerGroupIds(const std::string& playerUuid) {
    return m_pimpl->getPlayerGroupIds(playerUuid);
}

std::vector<std::string> PermissionManager::getPlayersInGroup(const std::string& groupName) {
    return m_pimpl->getPlayersInGroup(groupName);
}

std::vector<GroupDetails> PermissionManager::getPlayerGroupsWithPriorities(const std::string& playerUuid) {
    return m_pimpl->getPlayerGroupsWithPriorities(playerUuid);
}

size_t PermissionManager::addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames) {
    return m_pimpl->addPlayerToGroups(playerUuid, groupNames);
}

size_t
PermissionManager::removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames) {
    return m_pimpl->removePlayerFromGroups(playerUuid, groupNames);
}

std::vector<CompiledPermissionRule> PermissionManager::getAllPermissionsForPlayer(const std::string& playerUuid) {
    return m_pimpl->getAllPermissionsForPlayer(playerUuid);
}

bool PermissionManager::hasPermission(const std::string& playerUuid, const std::string& permissionNode) {
    return m_pimpl->hasPermission(playerUuid, permissionNode);
}


} // namespace permission
} // namespace BA