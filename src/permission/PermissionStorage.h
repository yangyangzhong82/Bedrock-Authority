#pragma once

#include "permission/PermissionData.h"
#include <string>
#include <unordered_map>
#include <vector>


namespace BA {
namespace db {
class IDatabase;
} // namespace db

namespace permission {
namespace internal {

/**
 * @brief 权限存储类，负责与数据库交互，管理权限、用户组、用户组权限、继承关系和玩家用户组数据。
 */
class PermissionStorage {
public:
    /**
     * @brief 构造函数。
     * @param db 数据库接口指针。
     */
    explicit PermissionStorage(db::IDatabase* db);

    /**
     * @brief 确保所有必要的数据库表都已创建。
     * @return 如果所有表都已成功创建或存在，则返回 true；否则返回 false。
     */
    bool ensureTables();

    // 权限管理
    /**
     * @brief 插入或更新权限。
     * @param name 权限名称。
     * @param description 权限描述。
     * @param defaultValue 权限的默认值。
     * @return 如果操作成功，则返回 true；否则返回 false。
     */
    bool upsertPermission(const std::string& name, const std::string& description, bool defaultValue);
    /**
     * @brief 检查权限是否存在。
     * @param name 权限名称。
     * @return 如果权限存在，则返回 true；否则返回 false。
     */
    bool permissionExists(const std::string& name);
    /**
     * @brief 获取所有权限的名称。
     * @return 包含所有权限名称的字符串向量。
     */
    std::vector<std::string>              fetchAllPermissionNames();
    /**
     * @brief 获取所有默认权限的名称。
     * @return 包含所有默认权限名称的字符串向量。
     */
    std::vector<std::string>              fetchDefaultPermissionNames();
    /**
     * @brief 获取所有权限及其默认值。
     * @return 权限名称到其默认值的映射。
     */
    std::unordered_map<std::string, bool> fetchAllPermissionDefaults();

    // 用户组管理
    /**
     * @brief 创建用户组。
     * @param groupName 用户组名称。
     * @param description 用户组描述。
     * @param outGroupId 输出参数，返回创建的用户组ID。
     * @return 如果用户组创建成功，则返回 true；否则返回 false。
     */
    bool        createGroup(const std::string& groupName, const std::string& description, std::string& outGroupId);
    /**
     * @brief 删除用户组。
     * @param groupId 用户组ID。
     * @return 如果用户组删除成功，则返回 true；否则返回 false。
     */
    bool        deleteGroup(const std::string& groupId);
    /**
     * @brief 根据用户组名称获取用户组ID。
     * @param groupName 用户组名称。
     * @return 用户组ID，如果不存在则返回空字符串。
     */
    std::string fetchGroupIdByName(const std::string& groupName);
    /**
     * @brief 获取所有用户组的名称。
     * @return 包含所有用户组名称的字符串向量。
     */
    std::vector<std::string> fetchAllGroupNames();
    /**
     * @brief 检查用户组是否存在。
     * @param groupName 用户组名称。
     * @return 如果用户组存在，则返回 true；否则返回 false。
     */
    bool                     groupExists(const std::string& groupName);
    /**
     * @brief 获取用户组的详细信息。
     * @param groupName 用户组名称。
     * @return 用户组详细信息对象。
     */
    GroupDetails             fetchGroupDetails(const std::string& groupName);
    /**
     * @brief 获取用户组的优先级。
     * @param groupName 用户组名称。
     * @return 用户组优先级。
     */
    int                      fetchGroupPriority(const std::string& groupName);
    /**
     * @brief 更新用户组优先级。
     * @param groupName 用户组名称。
     * @param priority 新的优先级。
     * @return 如果更新成功，则返回 true；否则返回 false。
     */
    bool                     updateGroupPriority(const std::string& groupName, int priority);
    /**
     * @brief 更新用户组描述。
     * @param groupName 用户组名称。
     * @param newDescription 新的描述。
     * @return 如果更新成功，则返回 true；否则返回 false。
     */
    bool                     updateGroupDescription(const std::string& groupName, const std::string& newDescription);
    /**
     * @brief 获取用户组描述。
     * @param groupName 用户组名称。
     * @return 用户组描述，如果不存在则返回空字符串。
     */
    std::string              fetchGroupDescription(const std::string& groupName);

    // 用户组权限管理
    /**
     * @brief 为用户组添加权限规则。
     * @param groupId 用户组ID。
     * @param permissionRule 权限规则。
     * @return 如果添加成功，则返回 true；否则返回 false。
     */
    bool                     addPermissionToGroup(const std::string& groupId, const std::string& permissionRule);
    /**
     * @brief 从用户组中移除权限规则。
     * @param groupId 用户组ID。
     * @param permissionRule 权限规则。
     * @return 如果移除成功，则返回 true；否则返回 false。
     */
    bool                     removePermissionFromGroup(const std::string& groupId, const std::string& permissionRule);
    /**
     * @brief 获取用户组的直接权限。
     * @param groupId 用户组ID。
     * @return 包含用户组直接权限规则的字符串向量。
     */
    std::vector<std::string> fetchDirectPermissionsOfGroup(const std::string& groupId);
    /**
     * @brief 为用户组批量添加权限规则。
     * @param groupId 用户组ID。
     * @param permissionRules 权限规则向量。
     * @return 成功添加的权限数量。
     */
    size_t addPermissionsToGroup(const std::string& groupId, const std::vector<std::string>& permissionRules);
    /**
     * @brief 从用户组中批量移除权限规则。
     * @param groupId 用户组ID。
     * @param permissionRules 权限规则向量。
     * @return 成功移除的权限数量。
     */
    size_t removePermissionsFromGroup(const std::string& groupId, const std::vector<std::string>& permissionRules);

    // 继承关系管理
    /**
     * @brief 添加用户组继承关系。
     * @param groupId 子用户组ID。
     * @param parentGroupId 父用户组ID。
     * @return 如果添加成功，则返回 true；否则返回 false。
     */
    bool addGroupInheritance(const std::string& groupId, const std::string& parentGroupId);
    /**
     * @brief 移除用户组继承关系。
     * @param groupId 子用户组ID。
     * @param parentGroupId 父用户组ID。
     * @return 如果移除成功，则返回 true；否则返回 false。
     */
    bool removeGroupInheritance(const std::string& groupId, const std::string& parentGroupId);
    /**
     * @brief 获取所有继承关系。
     * @return 父用户组ID到其子用户组ID集合的映射。
     */
    std::unordered_map<std::string, std::set<std::string>> fetchAllInheritance();
    /**
     * @brief 获取用户组的所有直接父用户组ID。
     * @param groupId 用户组ID。
     * @return 包含直接父用户组ID的字符串向量。
     */
    std::vector<std::string> fetchDirectParentGroupIds(const std::string& groupId);

    // 玩家用户组管理
    /**
     * @brief 将玩家添加到用户组。
     * @param playerUuid 玩家UUID。
     * @param groupId 用户组ID。
     * @return 如果添加成功，则返回 true；否则返回 false。
     */
    bool                      addPlayerToGroup(const std::string& playerUuid, const std::string& groupId);
    // 新增：带过期时间戳的版本 (使用 optional 来表示永不过期)
    bool addPlayerToGroup(
        const std::string&              playerUuid,
        const std::string&              groupId,
        const std::optional<long long>& expiryTimestamp
    );
    /**
     * @brief 将玩家从用户组中移除。
     * @param playerUuid 玩家UUID。
     * @param groupId 用户组ID。
     * @return 如果移除成功，则返回 true；否则返回 false。
     */
    bool                      removePlayerFromGroup(const std::string& playerUuid, const std::string& groupId);
    /**
     * @brief 获取玩家在特定用户组中的过期时间戳。
     * @param playerUuid 玩家UUID。
     * @param groupId 用户组ID。
     * @return 如果存在且有过期时间，则返回过期时间戳；如果永不过期或不存在，则返回 std::nullopt。
     */
    std::optional<long long> fetchPlayerGroupExpirationTime(const std::string& playerUuid, const std::string& groupId);

    /**
     * @brief 更新玩家在特定用户组中的过期时间戳。
     * @param playerUuid 玩家UUID。
     * @param groupId 用户组ID。
     * @param expiryTimestamp 新的过期时间戳。std::nullopt 表示永不过期。
     * @return 如果更新成功，则返回 true；否则返回 false。
     */
    bool updatePlayerGroupExpirationTime(
        const std::string&              playerUuid,
        const std::string&              groupId,
        const std::optional<long long>& expiryTimestamp
    );
        /**
         * @brief 获取玩家所属的用户组及其详细信息。
         * @param playerUuid 玩家UUID。
         * @return 包含玩家所属用户组详细信息的向量。
         */
        std::vector<GroupDetails> fetchPlayerGroupsWithDetails(const std::string& playerUuid);
    /**
     * @brief 获取指定用户组中的所有玩家UUID。
     * @param groupId 用户组ID。
     * @return 包含玩家UUID的字符串向量。
     */
    std::vector<std::string>  fetchPlayersInGroup(const std::string& groupId);
    /**
     * @brief 获取在多个用户组中的所有玩家UUID。
     * @param groupIds 用户组ID向量。
     * @return 包含玩家UUID的字符串向量。
     */
    std::vector<std::string>  fetchPlayersInGroups(const std::vector<std::string>& groupIds); // 新增：获取多个用户组中的玩家
    /**
     * @brief 根据用户组名称获取用户组ID。
     * @param groupNames 用户组名称集合。
     * @return 用户组名称到用户组ID的映射。
     */
    std::unordered_map<std::string, std::string> fetchGroupIdsByNames(const std::set<std::string>& groupNames); // 新增：根据名称获取用户组ID
    /**
     * @brief 根据用户组ID获取用户组名称。
     * @param groupIds 用户组ID向量。
     * @return 用户组ID到用户组名称的映射。
     */
    std::unordered_map<std::string, std::string> fetchGroupNamesByIds(const std::vector<std::string>& groupIds); // 新增：根据ID获取用户组名称
    /**
     * @brief 根据用户组名称获取用户组详细信息。
     * @param groupNames 用户组名称集合。
     * @return 用户组名称到用户组详细信息的映射。
     */
    std::unordered_map<std::string, GroupDetails> fetchGroupDetailsByNames(const std::set<std::string>& groupNames); // 新增：根据名称获取用户组详细信息

    /**
     * @brief 将玩家批量添加到多个用户组。
     * @param playerUuid 玩家UUID。
     * @param groupInfos 包含用户组名称和ID的pair向量。
     * @return 成功添加的玩家用户组关联数量。
     */
    size_t                    addPlayerToGroups(
                           const std::string&                                      playerUuid,
                           const std::vector<std::pair<std::string, std::string>>& groupInfos
                       ); // pair<groupName, groupId>
    /**
     * @brief 将玩家从多个用户组中批量移除。
     * @param playerUuid 玩家UUID。
     * @param groupIds 用户组ID向量。
     * @return 成功移除的玩家用户组关联数量。
     */
    size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupIds);
    // 新增：为后台任务删除所有过期的玩家组关系
    std::vector<std::string> deleteExpiredPlayerGroups();
    struct PlayerGroupInfo {
        std::string              groupId;
        std::optional<long long> expiryTimestamp;
    };

private:
    db::IDatabase* m_db = nullptr; /**< 数据库接口指针 */
};

} // namespace internal
} // namespace permission
} // namespace BA
