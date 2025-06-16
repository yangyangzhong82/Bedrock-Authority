#pragma once

#include "permission/PermissionData.h" // 包含公共数据结构
#include <memory>

namespace BA {
namespace db {
class IDatabase;
} // namespace db

namespace permission {

// The only public-facing class. It acts as a facade and uses the PIMPL idiom
// to hide all implementation details.
class PermissionManager {
public:
    BA_API static PermissionManager& getInstance();

    // --- Core Lifecycle ---
    BA_API bool init(db::IDatabase* db, bool enableWarmup = true, unsigned int threadPoolSize = 4);
    BA_API void shutdown();

    // --- Permission Management ---
    BA_API bool
    registerPermission(const std::string& name, const std::string& description = "", bool defaultValue = false);
    BA_API bool permissionExists(const std::string& name);
    BA_API std::vector<std::string> getAllPermissions();

    // --- Group Management ---
    BA_API bool createGroup(const std::string& groupName, const std::string& description = "");
    BA_API bool deleteGroup(const std::string& groupName);
    BA_API bool groupExists(const std::string& groupName);
    BA_API std::vector<std::string> getAllGroups();
    BA_API GroupDetails             getGroupDetails(const std::string& groupName);
    BA_API bool updateGroupDescription(const std::string& groupName, const std::string& newDescription);
    BA_API std::string getGroupDescription(const std::string& groupName);

    // --- Group Permissions ---
    BA_API bool addPermissionToGroup(const std::string& groupName, const std::string& permissionName);
    BA_API bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionName);
    BA_API std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName);
    BA_API std::vector<CompiledPermissionRule> getPermissionsOfGroup(const std::string& groupName);
    BA_API size_t addPermissionsToGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);
    BA_API size_t
    removePermissionsFromGroup(const std::string& groupName, const std::vector<std::string>& permissionRules);

    // --- Group Inheritance ---
    BA_API bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    BA_API bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName);
    BA_API std::vector<std::string> getAllAncestorGroups(const std::string& groupName);
    BA_API std::vector<std::string> getDirectParentGroups(const std::string& groupName);

    // --- Group Priority ---
    BA_API bool setGroupPriority(const std::string& groupName, int priority);
    BA_API int  getGroupPriority(const std::string& groupName);

    // --- Player Management ---
    BA_API bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName);
    BA_API bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName);
    BA_API std::vector<std::string> getPlayerGroups(const std::string& playerUuid);
    BA_API std::vector<std::string> getPlayerGroupIds(const std::string& playerUuid);
    BA_API std::vector<std::string> getPlayersInGroup(const std::string& groupName);
    BA_API std::vector<GroupDetails> getPlayerGroupsWithPriorities(const std::string& playerUuid);
    BA_API size_t addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);
    BA_API size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames);

    // --- Permission Check ---
    BA_API std::vector<CompiledPermissionRule> getAllPermissionsForPlayer(const std::string& playerUuid);
    BA_API bool hasPermission(const std::string& playerUuid, const std::string& permissionNode);

private:
    // PIMPL: Forward-declare the implementation class
    class PermissionManagerImpl;
    std::unique_ptr<PermissionManagerImpl> m_pimpl;

    // Private constructor/destructor to be defined in the .cpp file
    // This is necessary for std::unique_ptr with an incomplete type.
    PermissionManager();
    ~PermissionManager();

    // Disable copy/move to enforce singleton pattern
    PermissionManager(const PermissionManager&)            = delete;
    PermissionManager& operator=(const PermissionManager&) = delete;
    PermissionManager(PermissionManager&&)                 = delete;
    PermissionManager& operator=(PermissionManager&&)      = delete;
};

} // namespace permission
} // namespace BA
