# Bedrock Authority - PermissionManager API 文档

本文档介绍了如何使用 `BA::permission::PermissionManager` 类来管理权限、权限组和玩家权限。

**核心特性:**
*   **基于数据库:** 所有权限数据持久化存储在数据库中。
*   **单例模式:** 通过 `getInstance()` 获取全局唯一的管理器实例。
*   **缓存机制:** 内部使用缓存（如组名到 ID 的映射）来提高读取性能。写操作（如创建/删除组）会自动更新或失效相关缓存。
*   **线程安全:** 内部对缓存的访问使用了读写锁 (`std::shared_mutex`)，保证在多线程环境下的基本安全（读操作并发，写操作互斥）。
*   **权限规则:** 支持肯定、否定 (`-` 前缀) 和通配符 (`*`) 规则。
*   **组继承:** 支持权限组之间的多重继承。
*   **优先级:** 权限组可以设置优先级，影响权限冲突的解决。

## 1. 获取 PermissionManager 实例

`PermissionManager` 是一个单例类。你需要通过静态方法 `getInstance()` 来获取它的唯一实例：

```cpp
#include "permission/PermissionManager.h"

// 获取 PermissionManager 实例
BA::permission::PermissionManager& pm = BA::permission::PermissionManager::getInstance();
```

## 2. 初始化 PermissionManager

在使用任何其他 API 之前，必须使用一个有效的数据库实例来初始化 `PermissionManager`。数据库实例需要实现 `BA::db::IDatabase` 接口。

```cpp
#include "db/IDatabase.h" // 假设你有一个数据库实例 dbInstance

// 初始化 PermissionManager
// dbInstance 必须是一个指向实现了 IDatabase 接口的对象的指针
// init 函数会执行必要的数据库表检查和创建（如果表不存在），
// 尝试进行简单的模式迁移（如添加缺失的列），并填充内部缓存。
// 注意：复杂的数据库模式更改可能需要手动干预。
pm.init(dbInstance);
```

## 3. 权限管理 (Permissions)

### 3.1 注册权限 `registerPermission`

注册一个新的权限节点。

```cpp
// 注册一个名为 "myplugin.command.use" 的权限
// 可选提供描述和默认值 (默认为 false)
bool success = pm.registerPermission("myplugin.command.use", "允许使用 myplugin 的命令", false);
if (success) {
    // 注册成功
} else {
    // 权限已存在或发生错误
}
```

*   **参数:**
    *   `name` (std::string): 权限节点的名称 (例如 "myplugin.feature.access")。
    *   `description` (std::string, 可选): 权限的描述。
    *   `defaultValue` (bool, 可选): 权限的默认值 (通常为 `false`)。
*   **返回:** `bool` - 成功（包括插入新权限或更新现有权限的描述/默认值）返回 `true`，发生数据库错误则返回 `false`。此函数内部实现了 "upsert"（更新或插入）逻辑。

### 3.2 检查权限是否存在 `permissionExists`

检查指定的权限节点是否已被注册。

```cpp
bool exists = pm.permissionExists("myplugin.command.use");
if (exists) {
    // 权限存在
}
```

*   **参数:** `name` (std::string): 要检查的权限节点名称。
*   **返回:** `bool` - 权限节点已注册则返回 `true`，否则返回 `false`。

### 3.3 获取所有权限 `getAllPermissions`

获取所有已注册的权限节点名称列表。

```cpp
std::vector<std::string> allPermissions = pm.getAllPermissions();
for (const auto& perm : allPermissions) {
    // 处理权限名称 perm
}
```

*   **返回:** `std::vector<std::string>` - 包含所有已注册权限节点名称的向量。

## 4. 权限组管理 (Groups)

### 4.1 创建权限组 `createGroup`

创建一个新的权限组。

```cpp
bool success = pm.createGroup("admin", "管理员组");
if (success) {
    // 创建成功
} else {
    // 组已存在或发生错误
}
```

*   **参数:**
    *   `groupName` (std::string): 要创建的组的名称。
    *   `description` (std::string, 可选): 组的描述。
*   **返回:** `bool` - 成功创建组或组已存在（因为内部处理了重复键错误，效果类似 `INSERT IGNORE`）则返回 `true`，发生数据库错误则返回 `false`。成功时会自动查询组 ID 并更新内部组缓存。

### 4.2 检查组是否存在 `groupExists`

检查指定的权限组是否存在。

```cpp
bool exists = pm.groupExists("admin");
if (exists) {
    // 组存在
}
```

*   **参数:** `groupName` (std::string): 要检查的组名称。
*   **返回:** `bool` - 组存在则返回 `true`，否则返回 `false`。

### 4.3 获取所有组 `getAllGroups`

获取所有已创建的权限组名称列表。

```cpp
std::vector<std::string> allGroups = pm.getAllGroups();
for (const auto& group : allGroups) {
    // 处理组名称 group
}
```

*   **返回:** `std::vector<std::string>` - 包含所有已创建组名称的向量。

### 4.4 删除权限组 `deleteGroup`

删除一个权限组。**注意：** 这通常也会移除与该组相关的玩家分配和权限分配（具体行为取决于实现）。

```cpp
bool success = pm.deleteGroup("moderator");
if (success) {
    // 删除成功
} else {
    // 组不存在或发生错误
}
```

*   **参数:** `groupName` (std::string): 要删除的组名称。
*   **返回:** `bool` - 成功删除返回 `true`。如果组不存在（根据缓存或数据库检查），则返回 `false`。成功时会自动使内部组缓存失效。数据库的 `ON DELETE CASCADE` 约束会自动处理与该组相关的 `group_permissions`, `group_inheritance`, `player_groups` 条目。

## 5. 组权限管理 (Group Permissions)

权限规则 (Permission Rules) 是分配给组的字符串，可以是：
*   **肯定规则:** `myplugin.command.use` (授予权限)
*   **否定规则:** `-myplugin.admin.access` (明确拒绝权限，以 `-` 开头)
*   **通配符规则:** `myplugin.feature.*` 或 `-myplugin.other.*` (匹配多个权限节点)

### 5.1 向组添加权限规则 `addPermissionToGroup`

将一个权限规则字符串分配给一个权限组。

```cpp
// 添加肯定规则
bool success1 = pm.addPermissionToGroup("admin", "myplugin.command.use");
// 添加否定规则
bool success2 = pm.addPermissionToGroup("default", "-myplugin.admin");
// 添加通配符规则
bool success3 = pm.addPermissionToGroup("vip", "myplugin.vipfeatures.*");

if (success1) {
    // 添加成功
} else {
    // 组不存在，或规则已存在于该组（由于 INSERT IGNORE），或规则无效（空或仅"-")，或发生数据库错误
}
```

*   **参数:**
    *   `groupName` (std::string): 目标组的名称。
    *   `permissionRule` (std::string): 要添加的权限规则字符串 (例如 `"my.perm"`, `"-my.other.perm"`, `"plugin.*"`).
*   **返回:** `bool` - 成功添加或规则已存在于该组（由于使用了 `INSERT IGNORE` 语义）则返回 `true`。如果组不存在、规则无效（空或仅为"-") 或发生数据库错误，则返回 `false`。

### 5.2 从组移除权限规则 `removePermissionFromGroup`

从权限组中移除一个权限规则字符串。

```cpp
bool success = pm.removePermissionFromGroup("moderator", "-myplugin.feature.access"); // 移除否定规则
if (success) {
    // 移除成功
} else {
    // 组不存在，或规则未分配给该组，或规则无效，或发生数据库错误
}
```

*   **参数:**
    *   `groupName` (std::string): 目标组的名称。
    *   `permissionRule` (std::string): 要移除的权限规则字符串。
*   **返回:** `bool` - 成功移除返回 `true`。如果组不存在、规则无效、规则未分配给该组或发生数据库错误，则返回 `false`。

### 5.3 获取组的直接权限规则 `getDirectPermissionsOfGroup`

获取一个组直接分配的权限规则字符串列表（不包括通过继承获得的规则）。

```cpp
std::vector<std::string> directRules = pm.getDirectPermissionsOfGroup("vip");
for (const auto& rule : directRules) {
    // 处理直接规则 rule (可能包含 "-" 前缀或 "*")
}
```

*   **参数:** `groupName` (std::string): 目标组的名称。
*   **返回:** `std::vector<std::string>` - 包含该组直接分配的权限规则字符串的向量。如果组不存在，返回空向量。

### 5.4 获取组的最终权限规则 `getPermissionsOfGroup`

获取一个组最终生效的所有权限规则列表。

此函数通过深度优先搜索（DFS）遍历组的继承链（包括父组、父组的父组等），收集所有直接分配和继承而来的权限规则字符串。然后进行解析：
*   收集所有规则（肯定、否定、通配符）。
*   处理规则冲突：**否定规则 (`-perm.node`) 具有最高优先级**。一旦某个基础权限节点 (`perm.node`) 被任何直接或间接继承的否定规则所否定，那么即使存在针对该节点或其通配符 (`perm.*`) 的肯定规则，该节点最终也会被视为**无效/未授予**。
*   返回的列表包含了经过此解析后仍然有效的规则（包括带 `-` 前缀的否定规则和通配符规则）。

```cpp
std::vector<std::string> finalRules = pm.getPermissionsOfGroup("admin");
for (const auto& rule : finalRules) {
    // 处理最终生效的规则 rule (可能包含 "-" 前缀或 "*")
    // 例如，如果 admin 继承 default，default 有 "a.b" 和 "-a.c"，admin 直接拥有 "a.c"。
    // 由于 "-a.c" 的存在（来自继承），最终规则列表将不包含 "a.c" 或 "-a.c" 的肯定形式。
    // 它可能包含 "a.b" 和 "-a.c" （如果实现返回所有解析后的规则）。
    // 或者，如果只返回最终状态，可能只包含 "a.b"。
    // 当前实现返回的是解析后的规则列表，包括有效的否定规则。
}
```

*   **参数:** `groupName` (std::string): 目标组的名称。
*   **返回:** `std::vector<std::string>` - 包含该组所有最终生效权限规则（已解析继承和否定优先级）的向量。如果组不存在，返回空向量。

## 6. 组继承管理 (Group Inheritance)

### 6.1 添加组继承 `addGroupInheritance`

设置一个组（子组）继承另一个组（父组）的权限。

```cpp
// 让 moderator 组继承 default 组的权限
bool success = pm.addGroupInheritance("moderator", "default");
if (success) {
    // 添加继承成功
} else {
    // 组不存在，或继承关系已存在/形成循环，或发生错误
}
```

*   **参数:**
    *   `groupName` (std::string): 子组的名称。
    *   `parentGroupName` (std::string): 父组的名称。
*   **返回:** `bool` - 成功添加继承关系或关系已存在（由于 `INSERT IGNORE`）则返回 `true`。如果组不存在、尝试自我继承或发生数据库错误，则返回 `false`。

### 6.2 移除组继承 `removeGroupInheritance`

移除一个组的继承关系。

```cpp
bool success = pm.removeGroupInheritance("moderator", "default");
if (success) {
    // 移除继承成功
} else {
    // 组不存在，或继承关系不存在，或发生错误
}
```

*   **参数:**
    *   `groupName` (std::string): 子组的名称。
    *   `parentGroupName` (std::string): 父组的名称。
*   **返回:** `bool` - 成功移除继承关系返回 `true`。如果组不存在或继承关系不存在，则返回 `false`。

### 6.3 获取父组 `getParentGroups`

获取一个组直接继承的父组列表。

```cpp
std::vector<std::string> parents = pm.getParentGroups("moderator");
for (const auto& parent : parents) {
    // 处理父组名称 parent
}
```

*   **参数:** `groupName` (std::string): 子组的名称。
*   **返回:** `std::vector<std::string>` - 包含直接父组名称的向量。如果组不存在或没有父组，返回空向量。

## 7. 玩家与组管理 (Player Groups)

### 7.1 将玩家添加到组 `addPlayerToGroup`

将一个玩家分配到一个权限组。

```cpp
std::string playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"; // 玩家的 UUID
bool success = pm.addPlayerToGroup(playerUuid, "vip");
if (success) {
    // 添加成功
} else {
    // 玩家或组不存在，或玩家已在该组，或发生错误
}
```

*   **参数:**
    *   `playerUuid` (std::string): 玩家的唯一标识符 (UUID)。
    *   `groupName` (std::string): 目标组的名称。
*   **返回:** `bool` - 成功添加或玩家已在该组（由于 `INSERT IGNORE`）则返回 `true`。如果组不存在或发生数据库错误，则返回 `false`。

### 7.2 从组移除玩家 `removePlayerFromGroup`

将一个玩家从权限组中移除。

```cpp
std::string playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
bool success = pm.removePlayerFromGroup(playerUuid, "vip");
if (success) {
    // 移除成功
} else {
    // 玩家或组不存在，或玩家不在该组，或发生错误
}
```

*   **参数:**
    *   `playerUuid` (std::string): 玩家的 UUID。
    *   `groupName` (std::string): 目标组的名称。
*   **返回:** `bool` - 成功移除返回 `true`。如果组不存在或玩家不在该组（根据数据库检查），则返回 `false`。

### 7.3 获取玩家所属的组 `getPlayerGroups`

获取一个玩家所属的所有权限组的名称列表。

```cpp
std::string playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
std::vector<std::string> playerGroups = pm.getPlayerGroups(playerUuid);
for (const auto& group : playerGroups) {
    // 处理玩家所属的组 group
}
```

*   **参数:** `playerUuid` (std::string): 玩家的 UUID。
*   **返回:** `std::vector<std::string>` - 包含玩家所属组名称的向量。如果玩家不属于任何组，返回空向量。

### 7.4 获取玩家所属的组 ID `getPlayerGroupIds`

获取一个玩家所属的所有权限组的 ID 列表（字符串形式）。

```cpp
std::string playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
std::vector<std::string> playerGroupIds = pm.getPlayerGroupIds(playerUuid);
for (const auto& groupId : playerGroupIds) {
    // 处理玩家所属的组 ID groupId
}
```

*   **参数:** `playerUuid` (std::string): 玩家的 UUID。
*   **返回:** `std::vector<std::string>` - 包含玩家所属组 ID 的向量。如果玩家不属于任何组，返回空向量。

### 7.5 获取组内玩家 `getPlayersInGroup`

获取一个权限组内所有玩家的 UUID 列表。

```cpp
std::vector<std::string> players = pm.getPlayersInGroup("admin");
for (const auto& uuid : players) {
    // 处理组内玩家 uuid
}
```

*   **参数:** `groupName` (std::string): 目标组的名称。
*   **返回:** `std::vector<std::string>` - 包含该组内所有玩家 UUID 的向量。如果组不存在或组内没有玩家，返回空向量。

## 8. 玩家权限检查 (Player Permissions)

### 8.1 获取玩家所有最终权限节点 `getAllPermissionsForPlayer`

计算并获取一个玩家最终实际拥有的所有**已注册**权限节点名称列表。

这个过程综合考虑了默认权限、玩家所属的所有组、组的优先级、组的继承关系、肯定/否定规则以及通配符：
1.  获取所有已在 `permissions` 表中注册的权限节点名称。
2.  获取所有 `default_value = true` 的已注册权限，将它们作为玩家的基础权限集合。
3.  获取玩家所属的所有组，并根据 `priority` 字段进行降序排序（高优先级在前）。
4.  按优先级顺序遍历玩家的组：
    *   对每个组，调用 `getPermissionsOfGroup` 获取其最终生效的权限规则列表（此列表已处理了该组自身的继承和否定规则优先级）。
    *   收集来自所有这些组的肯定规则和否定规则。
5.  **处理肯定规则:**
    *   对于精确的肯定规则 (如 `"my.perm"`), 如果它对应一个已注册的节点，则尝试将其添加到玩家的最终权限集合中。
    *   对于通配符肯定规则 (如 `"plugin.*"`), 将其与所有已注册的节点进行匹配，并将所有匹配的节点尝试添加到玩家的最终权限集合中。
6.  **处理否定规则:**
    *   对于精确的否定规则 (如 `"-my.other.perm"`), 从玩家的最终权限集合中移除对应的节点 (`"my.other.perm"`)。
    *   对于通配符否定规则 (如 `"-plugin.admin.*"`), 将其与玩家当前拥有的最终权限集合中的节点进行匹配，并移除所有匹配的节点。
7.  返回最终计算出的权限节点集合。这个集合只包含玩家被明确授予且未被更高优先级规则否定的**已注册**权限节点名称（不包含 `-` 前缀或通配符）。

```cpp
std::string playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
std::vector<std::string> playerNodes = pm.getAllPermissionsForPlayer(playerUuid);
for (const auto& node : playerNodes) {
    // 处理玩家最终拥有的权限节点 node (例如 "myplugin.command.use")
}
```

*   **参数:** `playerUuid` (std::string): 玩家的 UUID。
*   **返回:** `std::vector<std::string>` - 包含玩家最终拥有的所有已注册权限节点名称的向量（已排序）。

### 8.2 检查玩家是否拥有特定权限 `hasPermission`

检查一个玩家是否最终拥有特定的、**已注册的**权限节点。

检查逻辑按以下优先级顺序进行：
1.  **组规则优先:**
    *   获取玩家所属的所有组，并按优先级（高到低）排序。
    *   按顺序遍历这些组。对每个组，获取其最终生效的权限规则列表 (`getPermissionsOfGroup`)。
    *   检查这些规则中是否有**第一个**与请求的 `permissionNode` 匹配的规则（使用正则表达式处理规则中的通配符 `*`）。
    *   如果找到第一个匹配规则：
        *   如果该规则是否定规则 (`-` 开头)，则判定玩家**没有**权限，立即返回 `false`。
        *   如果该规则是肯定规则，则判定玩家**拥有**权限，立即返回 `true`。
2.  **默认值次之:** 如果遍历完所有组都没有找到明确的匹配规则，则查找该 `permissionNode` 在 `permissions` 表中定义的 `default_value`。
    *   如果 `default_value` 为 `true` (1)，返回 `true`。
    *   如果 `default_value` 为 `false` (0)，返回 `false`。
3.  **最终默认拒绝:** 如果权限节点未在 `permissions` 表中注册（找不到 `default_value`），或者查找/解析 `default_value` 出错，则最终默认判定为**没有**权限，返回 `false`。

```cpp
std::string playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";

bool hasCmdUse = pm.hasPermission(playerUuid, "myplugin.command.use");
// 注意：hasPermission 不直接支持检查通配符权限，你需要检查具体的节点。
// 要检查玩家是否有任何 myplugin 权限，你需要调用 getAllPermissionsForPlayer 并自己检查结果。

if (hasCmdUse) {
    // 玩家拥有 myplugin.command.use 权限
}
```

*   **参数:**
    *   `playerUuid` (std::string): 玩家的 UUID。
    *   `permissionNode` (std::string): 要检查的**具体**权限节点 (例如 `"myplugin.command.use"`)。**不支持**在此处使用通配符。
*   **返回:** `bool` - 根据上述优先级逻辑，如果玩家最终被判定拥有该权限，则返回 `true`，否则返回 `false`。

## 9. 组优先级管理 (Group Priority)

### 9.1 设置组优先级 `setGroupPriority`

设置权限组的优先级。优先级是一个整数，数值越高，优先级越高。在权限冲突时（例如，一个组授予权限，另一个组否定该权限），优先级高的组的设置会覆盖优先级低的组。

```cpp
bool success = pm.setGroupPriority("admin", 100); // 设置 admin 组优先级为 100
if (success) {
    // 设置成功
} else {
    // 组不存在或发生错误
}
```

*   **参数:**
    *   `groupName` (std::string): 目标组的名称。
    *   `priority` (int): 组的优先级。
*   **返回:** `bool` - 成功设置返回 `true`。如果组不存在（通过 `groupExists` 检查）或发生数据库错误，则返回 `false`。

### 9.2 获取组优先级 `getGroupPriority`

获取权限组的优先级。

```cpp
int priority = pm.getGroupPriority("admin");
// 检查组是否存在不是必需的，因为如果组不存在，函数会返回 0。
if (pm.groupExists("admin")) { // 可以选择先检查，以区分“不存在”和“优先级为0”
   // 获取到优先级 priority
   // priority 可能为 0
} else {
   // 组不存在 (getGroupPriority 会返回 0)
}

```

*   **参数:** `groupName` (std::string): 目标组的名称。
*   **返回:** `int` - 组的优先级。如果组不存在或数据库中存储的优先级值无效，则返回 `0`。
