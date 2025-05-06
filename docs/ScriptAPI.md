# Bedrock Authority - 脚本 API 文档

本文档介绍了 Bedrock Authority 插件通过 `RemoteCallAPI` 导出的 JavaScript 脚本 API。所有函数都在 `BA` 命名空间下。

## 如何使用

在你的 JavaScript 插件中，你可以使用 `ll.import(namespace, functionName)` 来获取导出的函数。

```javascript
// 示例：导入并使用 hasPermission 函数
const hasPermission = ll.import("BA", "hasPermission");

const playerUuid = "some-player-uuid"; // 替换为实际玩家 UUID
const permissionNode = "myplugin.feature.use";

if (hasPermission(playerUuid, permissionNode)) {
    logger.info(`玩家 ${playerUuid} 拥有权限 ${permissionNode}`);
} else {
    logger.info(`玩家 ${playerUuid} 没有权限 ${permissionNode}`);
}
```

---

## 权限管理 (Permissions)

### `registerPermission(name, description, defaultValue)`

注册一个新的权限节点。

-   **参数:**
    -   `name` (String): 权限节点的名称 (例如: "myplugin.command.use")。
    -   `description` (String, 可选): 权限的描述。默认为空字符串。
    -   `defaultValue` (Boolean, 可选): 权限的默认值 (通常为 false)。默认为 false。
-   **返回:** (Boolean): 如果注册成功则返回 `true`，如果权限已存在或发生错误则返回 `false`。
-   **示例:**
    ```javascript
    const registerPermission = ll.import("BA", "registerPermission");
    const success = registerPermission("myplugin.feature.vip", "VIP 特权", false);
    if (success) {
        logger.info("权限 'myplugin.feature.vip' 注册成功");
    }
    ```

### `permissionExists(name)`

检查指定的权限节点是否存在。

-   **参数:**
    -   `name` (String): 要检查的权限节点名称。
-   **返回:** (Boolean): 如果权限存在则返回 `true`，否则返回 `false`。
-   **示例:**
    ```javascript
    const permissionExists = ll.import("BA", "permissionExists");
    if (permissionExists("myplugin.feature.vip")) {
        logger.info("权限 'myplugin.feature.vip' 已存在");
    }
    ```

### `getAllPermissions()`

获取所有已注册的权限节点名称列表。

-   **参数:** 无
-   **返回:** (Array<String>): 包含所有权限节点名称的字符串数组。
-   **示例:**
    ```javascript
    const getAllPermissions = ll.import("BA", "getAllPermissions");
    const permissions = getAllPermissions();
    logger.info("所有权限:", permissions.join(", "));
    ```

---

## 权限组管理 (Groups)

### `createGroup(groupName, description)`

创建一个新的权限组。

-   **参数:**
    -   `groupName` (String): 要创建的权限组名称。
    -   `description` (String, 可选): 权限组的描述。默认为空字符串。
-   **返回:** (Boolean): 如果创建成功则返回 `true`，如果组已存在或发生错误则返回 `false`。
-   **示例:**
    ```javascript
    const createGroup = ll.import("BA", "createGroup");
    createGroup("vip", "VIP 玩家组");
    ```

### `groupExists(groupName)`

检查指定的权限组是否存在。

-   **参数:**
    -   `groupName` (String): 要检查的权限组名称。
-   **返回:** (Boolean): 如果组存在则返回 `true`，否则返回 `false`。
-   **示例:**
    ```javascript
    const groupExists = ll.import("BA", "groupExists");
    if (groupExists("admin")) {
        logger.info("权限组 'admin' 已存在");
    }
    ```

### `getAllGroups()`

获取所有权限组的名称列表。

-   **参数:** 无
-   **返回:** (Array<String>): 包含所有权限组名称的字符串数组。
-   **示例:**
    ```javascript
    const getAllGroups = ll.import("BA", "getAllGroups");
    const groups = getAllGroups();
    logger.info("所有权限组:", groups.join(", "));
    ```

### `deleteGroup(groupName)`

删除一个权限组。

-   **参数:**
    -   `groupName` (String): 要删除的权限组名称。
-   **返回:** (Boolean): 如果删除成功则返回 `true`，如果组不存在或发生错误则返回 `false`。
-   **示例:**
    ```javascript
    const deleteGroup = ll.import("BA", "deleteGroup");
    deleteGroup("old_group");
    ```

### `addPermissionToGroup(groupName, permissionName)`

将一个权限节点添加到一个权限组。

-   **参数:**
    -   `groupName` (String): 目标权限组的名称。
    -   `permissionName` (String): 要添加的权限节点名称。
-   **返回:** (Boolean): 如果添加成功则返回 `true`，如果组或权限不存在，或权限已在该组中，则返回 `false`。
-   **示例:**
    ```javascript
    const addPermissionToGroup = ll.import("BA", "addPermissionToGroup");
    addPermissionToGroup("vip", "myplugin.feature.vip");
    ```

### `removePermissionFromGroup(groupName, permissionName)`

从一个权限组中移除一个权限节点。

-   **参数:**
    -   `groupName` (String): 目标权限组的名称。
    -   `permissionName` (String): 要移除的权限节点名称。
-   **返回:** (Boolean): 如果移除成功则返回 `true`，否则返回 `false`。
-   **示例:**
    ```javascript
    const removePermissionFromGroup = ll.import("BA", "removePermissionFromGroup");
    removePermissionFromGroup("vip", "myplugin.feature.old");
    ```

### `getDirectPermissionsOfGroup(groupName)`

获取一个权限组直接拥有的权限节点列表（不包括继承的权限）。

-   **参数:**
    -   `groupName` (String): 权限组的名称。
-   **返回:** (Array<String>): 包含该组直接权限节点名称的字符串数组。如果组不存在则返回空数组。
-   **示例:**
    ```javascript
    const getDirectPermissionsOfGroup = ll.import("BA", "getDirectPermissionsOfGroup");
    const directPerms = getDirectPermissionsOfGroup("admin");
    logger.info("Admin 组直接权限:", directPerms.join(", "));
    ```

### `getPermissionsOfGroup(groupName)`

获取一个权限组最终生效的所有权限节点列表（包括继承的权限）。

-   **参数:**
    -   `groupName` (String): 权限组的名称。
-   **返回:** (Array<String>): 包含该组所有生效权限节点名称的字符串数组。如果组不存在则返回空数组。
-   **示例:**
    ```javascript
    const getPermissionsOfGroup = ll.import("BA", "getPermissionsOfGroup");
    const allPerms = getPermissionsOfGroup("vip");
    logger.info("VIP 组所有权限:", allPerms.join(", "));
    ```

### `addGroupInheritance(groupName, parentGroupName)`

添加权限组继承关系，使 `groupName` 继承 `parentGroupName` 的所有权限。

-   **参数:**
    -   `groupName` (String): 子权限组的名称。
    -   `parentGroupName` (String): 父权限组的名称。
-   **返回:** (Boolean): 如果添加继承成功则返回 `true`，如果组不存在、继承关系已存在或形成循环继承，则返回 `false`。
-   **示例:**
    ```javascript
    const addGroupInheritance = ll.import("BA", "addGroupInheritance");
    addGroupInheritance("moderator", "player"); // moderator 继承 player 组
    ```

### `removeGroupInheritance(groupName, parentGroupName)`

移除权限组之间的继承关系。

-   **参数:**
    -   `groupName` (String): 子权限组的名称。
    -   `parentGroupName` (String): 父权限组的名称。
-   **返回:** (Boolean): 如果移除成功则返回 `true`，否则返回 `false`。
-   **示例:**
    ```javascript
    const removeGroupInheritance = ll.import("BA", "removeGroupInheritance");
    removeGroupInheritance("moderator", "old_parent");
    ```

### `getParentGroups(groupName)`

获取一个权限组直接继承的父权限组列表。

-   **参数:**
    -   `groupName` (String): 权限组的名称。
-   **返回:** (Array<String>): 包含直接父权限组名称的字符串数组。
-   **示例:**
    ```javascript
    const getParentGroups = ll.import("BA", "getParentGroups");
    const parents = getParentGroups("moderator");
    logger.info("Moderator 的父组:", parents.join(", "));
    ```

### `setGroupPriority(groupName, priority)`

设置权限组的优先级。当玩家属于多个组时，优先级高的组的权限规则（尤其是否定权限）会优先生效。

-   **参数:**
    -   `groupName` (String): 权限组的名称。
    -   `priority` (Number): 优先级数值，整数。数值越大，优先级越高。
-   **返回:** (Boolean): 如果设置成功则返回 `true`，否则返回 `false`。
-   **示例:**
    ```javascript
    const setGroupPriority = ll.import("BA", "setGroupPriority");
    setGroupPriority("admin", 100);
    setGroupPriority("vip", 50);
    ```

### `getGroupPriority(groupName)`

获取权限组的优先级。

-   **参数:**
    -   `groupName` (String): 权限组的名称。
-   **返回:** (Number): 权限组的优先级。如果组不存在，可能返回 0 或其他默认值（具体取决于实现）。
-   **示例:**
    ```javascript
    const getGroupPriority = ll.import("BA", "getGroupPriority");
    const priority = getGroupPriority("admin");
    logger.info("Admin 组优先级:", priority);
    ```

---

## 玩家权限管理 (Player Permissions)

### `addPlayerToGroup(playerUuid, groupName)`

将一个玩家添加到一个权限组。

-   **参数:**
    -   `playerUuid` (String): 玩家的 UUID。
    -   `groupName` (String): 要将玩家添加到的权限组名称。
-   **返回:** (Boolean): 如果添加成功则返回 `true`，如果玩家已在该组或发生错误则返回 `false`。
-   **示例:**
    ```javascript
    const addPlayerToGroup = ll.import("BA", "addPlayerToGroup");
    addPlayerToGroup("player-uuid-123", "vip");
    ```

### `removePlayerFromGroup(playerUuid, groupName)`

从一个权限组中移除一个玩家。

-   **参数:**
    -   `playerUuid` (String): 玩家的 UUID。
    -   `groupName` (String): 要从中移除玩家的权限组名称。
-   **返回:** (Boolean): 如果移除成功则返回 `true`，否则返回 `false`。
-   **示例:**
    ```javascript
    const removePlayerFromGroup = ll.import("BA", "removePlayerFromGroup");
    removePlayerFromGroup("player-uuid-123", "default");
    ```

### `getPlayerGroups(playerUuid)`

获取一个玩家所属的所有权限组的名称列表。

-   **参数:**
    -   `playerUuid` (String): 玩家的 UUID。
-   **返回:** (Array<String>): 包含玩家所属权限组名称的字符串数组。
-   **示例:**
    ```javascript
    const getPlayerGroups = ll.import("BA", "getPlayerGroups");
    const playerGroups = getPlayerGroups("player-uuid-123");
    logger.info("玩家所属组:", playerGroups.join(", "));
    ```

### `getPlayerGroupIds(playerUuid)`

获取一个玩家所属的所有权限组的 ID 列表（字符串形式）。（注意：通常脚本 API 使用名称更方便，此函数可能较少使用）。

-   **参数:**
    -   `playerUuid` (String): 玩家的 UUID。
-   **返回:** (Array<String>): 包含玩家所属权限组 ID 的字符串数组。
-   **示例:**
    ```javascript
    const getPlayerGroupIds = ll.import("BA", "getPlayerGroupIds");
    const groupIds = getPlayerGroupIds("player-uuid-123");
    logger.info("玩家所属组 ID:", groupIds.join(", "));
    ```

### `getPlayersInGroup(groupName)`

获取一个权限组中的所有玩家 UUID 列表。

-   **参数:**
    -   `groupName` (String): 权限组的名称。
-   **返回:** (Array<String>): 包含该组中所有玩家 UUID 的字符串数组。
-   **示例:**
    ```javascript
    const getPlayersInGroup = ll.import("BA", "getPlayersInGroup");
    const vipPlayers = getPlayersInGroup("vip");
    logger.info("VIP 组成员:", vipPlayers.join(", "));
    ```

### `getAllPermissionsForPlayer(playerUuid)`

获取一个玩家最终生效的所有权限节点列表（考虑了所有组、继承、优先级和否定规则）。

-   **参数:**
    -   `playerUuid` (String): 玩家的 UUID。
-   **返回:** (Array<String>): 包含玩家所有生效权限节点名称的字符串数组。
-   **示例:**
    ```javascript
    const getAllPermissionsForPlayer = ll.import("BA", "getAllPermissionsForPlayer");
    const playerPerms = getAllPermissionsForPlayer("player-uuid-123");
    logger.info("玩家最终权限:", playerPerms.join(", "));
    ```

### `hasPermission(playerUuid, permissionNode)`

检查一个玩家是否拥有特定的权限节点。这是最常用的权限检查函数。支持通配符 (`*`) 和否定权限 (`-` 前缀)。

-   **参数:**
    -   `playerUuid` (String): 玩家的 UUID。
    -   `permissionNode` (String): 要检查的权限节点 (例如: "myplugin.command.use", "myplugin.feature.*", "-myplugin.admin.ban")。
-   **返回:** (Boolean): 如果玩家拥有该权限（考虑所有规则后）则返回 `true`，否则返回 `false`。
-   **示例:**
    ```javascript
    const hasPermission = ll.import("BA", "hasPermission");

    // 检查具体权限
    if (hasPermission("player-uuid-123", "essentials.command.home")) {
        // ...
    }

    // 检查通配符权限
    if (hasPermission("player-uuid-123", "myplugin.admin.*")) {
        // ...
    }

    // 检查是否被明确禁止 (虽然通常直接检查肯定权限更常见)
    if (!hasPermission("player-uuid-123", "-essentials.command.ban")) {
       // 玩家没有被禁止 essentials.command.ban (但这不代表他一定有 ban 权限)
    }
    // 更常见的检查方式
    if (hasPermission("player-uuid-123", "essentials.command.ban")) {
        // 玩家有 ban 权限
    } else {
        // 玩家没有 ban 权限 (可能因为没分配，或被否定了)
    }
