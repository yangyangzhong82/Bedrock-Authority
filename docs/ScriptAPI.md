# Bedrock Authority 脚本 API 文档

本文档说明如何通过 `LegacyRemoteCall` 在脚本插件（如 LegacyScriptEngine / JavaScript）中调用 Bedrock Authority 导出的接口。

## 使用前提

1. 服务端需要同时安装并加载 `LegacyRemoteCall`（BA 启动时如果检测到 `LegacyRemoteCall.dll` 未加载，会自动跳过脚本 API 导出）。
2. 所有函数统一导出在 **`BA`** 命名空间下，通过 `ll.import("BA", "<函数名>")` 导入。
3. **参数类型与数量必须严格匹配**：RemoteCall 不支持省略参数，例如 `registerPermission` 必须传齐 3 个参数、`createGroup` 必须传齐 2 个参数。

```javascript
const hasPermission = ll.import("BA", "hasPermission");
const registerPermission = ll.import("BA", "registerPermission");

// 插件启动时注册权限节点
registerPermission("myplugin.feature.fly", "允许使用飞行功能", false);

// 业务中检查玩家权限
mc.listen("onUseItem", (player) => {
    if (hasPermission(player.uuid, "myplugin.feature.fly")) {
        player.tell("你拥有飞行权限");
    } else {
        player.tell("你没有飞行权限");
    }
});
```

---

## 接口速查表

| 分类 | 函数签名 | 返回类型 |
| --- | --- | --- |
| 权限节点 | `registerPermission(name, description, defaultValue)` | `Boolean` |
| 权限节点 | `permissionExists(name)` | `Boolean` |
| 权限节点 | `getAllPermissions()` | `Array<String>` |
| 权限组 | `createGroup(groupName, description)` | `Boolean` |
| 权限组 | `groupExists(groupName)` | `Boolean` |
| 权限组 | `getAllGroups()` | `Array<String>` |
| 权限组 | `deleteGroup(groupName)` | `Boolean` |
| 组权限 | `addPermissionToGroup(groupName, permissionName)` | `Boolean` |
| 组权限 | `removePermissionFromGroup(groupName, permissionName)` | `Boolean` |
| 组权限 | `getDirectPermissionsOfGroup(groupName)` | `Array<String>` |
| 组权限 | `getPermissionsOfGroup(groupName)` | `Array<String>` |
| 组继承与优先级 | `addGroupInheritance(groupName, parentGroupName)` | `Boolean` |
| 组继承与优先级 | `removeGroupInheritance(groupName, parentGroupName)` | `Boolean` |
| 组继承与优先级 | `getParentGroups(groupName)` | `Array<String>` |
| 组继承与优先级 | `setGroupPriority(groupName, priority)` | `Boolean` |
| 组继承与优先级 | `getGroupPriority(groupName)` | `Number` |
| 玩家与检查 | `addPlayerToGroup(playerUuid, groupName)` | `Boolean` |
| 玩家与检查 | `removePlayerFromGroup(playerUuid, groupName)` | `Boolean` |
| 玩家与检查 | `getPlayerGroups(playerUuid)` | `Array<String>` |
| 玩家与检查 | `getPlayerGroupIds(playerUuid)` | `Array<String>` |
| 玩家与检查 | `getPlayersInGroup(groupName)` | `Array<String>` |
| 玩家与检查 | `getAllPermissionsForPlayer(playerUuid)` | `Array<String>` |
| 玩家与检查 | `hasPermission(playerUuid, permissionNode)` | `Boolean` |

---

## 权限节点管理

### `registerPermission(name, description, defaultValue)`

注册或更新一个权限节点。若节点已存在，会更新其描述和默认值。

- **参数**：
  - `name` (`String`)：权限节点名称，例如 `"myplugin.command.fly"`。
  - `description` (`String`)：权限描述，无描述时传 `""`。
  - `defaultValue` (`Boolean`)：没有任何规则匹配时的默认结果，通常为 `false`。
- **返回**：`Boolean`，成功返回 `true`，失败返回 `false`。
- **示例**：
  ```javascript
  const registerPermission = ll.import("BA", "registerPermission");
  registerPermission("myplugin.feature.vip", "VIP 特权", false);
  ```

### `permissionExists(name)`

检查指定权限节点是否已注册。

- **参数**：`name` (`String`) — 权限节点名称。
- **返回**：`Boolean`。

### `getAllPermissions()`

获取所有已注册的权限节点名称列表。

- **参数**：无。
- **返回**：`Array<String>`。

---

## 权限组管理

### `createGroup(groupName, description)`

创建一个新的权限组（默认优先级为 `0`）。

- **参数**：
  - `groupName` (`String`)：权限组名称。
  - `description` (`String`)：权限组描述，无描述时传 `""`。
- **返回**：`Boolean`，组已存在或创建失败时返回 `false`。
- **示例**：
  ```javascript
  const createGroup = ll.import("BA", "createGroup");
  createGroup("vip", "VIP 玩家组");
  ```

### `groupExists(groupName)`

检查权限组是否存在。

- **参数**：`groupName` (`String`)。
- **返回**：`Boolean`。

### `getAllGroups()`

获取所有权限组名称列表。

- **参数**：无。
- **返回**：`Array<String>`。

### `deleteGroup(groupName)`

删除一个权限组及其关联数据。

- **参数**：`groupName` (`String`)。
- **返回**：`Boolean`，组不存在或删除失败返回 `false`。

---

## 组权限管理

### `addPermissionToGroup(groupName, permissionName)`

向权限组添加一条权限规则。支持通配符（如 `"myplugin.*"`）和反向拒绝（如 `"-myplugin.ban"`），不要求权限节点提前注册。

- **参数**：
  - `groupName` (`String`)：目标权限组名称。
  - `permissionName` (`String`)：要添加的权限规则字符串。
- **返回**：`Boolean`，组不存在、规则已存在或事件被取消时返回 `false`。
- **示例**：
  ```javascript
  const addPermissionToGroup = ll.import("BA", "addPermissionToGroup");
  addPermissionToGroup("vip", "myplugin.feature.*");
  addPermissionToGroup("vip", "-myplugin.feature.admin");
  ```

### `removePermissionFromGroup(groupName, permissionName)`

从权限组移除一条直接配置的权限规则。移除反向规则时需要带 `-` 前缀。

- **参数**：
  - `groupName` (`String`)：目标权限组名称。
  - `permissionName` (`String`)：要移除的规则字符串。
- **返回**：`Boolean`。

### `getDirectPermissionsOfGroup(groupName)`

获取权限组**直接拥有**的原始权限规则列表（不含继承，保留 `-` 前缀）。

- **参数**：`groupName` (`String`)。
- **返回**：`Array<String>`，组不存在时返回空数组。

### `getPermissionsOfGroup(groupName)`

获取权限组展开继承后合并得到的规则模式列表。

- **参数**：`groupName` (`String`)。
- **返回**：`Array<String>`，组不存在时返回空数组。
- **注意**：当前脚本接口返回的是编译后规则的 `pattern` 字段，不包含允许/拒绝状态（即反向规则不带 `-` 前缀）。如果需要判断某项权限是否允许，请使用 `hasPermission`。

---

## 组继承与优先级

### `addGroupInheritance(groupName, parentGroupName)`

让 `groupName` 继承 `parentGroupName` 的全部权限。

- **参数**：
  - `groupName` (`String`)：子组名称。
  - `parentGroupName` (`String`)：父组名称。
- **返回**：`Boolean`，组不存在、关系已存在或会造成循环继承时返回 `false`。

### `removeGroupInheritance(groupName, parentGroupName)`

移除 `groupName` 对 `parentGroupName` 的直接继承关系。

- **参数**：
  - `groupName` (`String`)：子组名称。
  - `parentGroupName` (`String`)：父组名称。
- **返回**：`Boolean`。

### `getParentGroups(groupName)`

获取权限组**直接继承**的父组名称列表。

- **参数**：`groupName` (`String`)。
- **返回**：`Array<String>`。

### `setGroupPriority(groupName, priority)`

设置权限组优先级。整数，数值越大优先级越高；多组存在同名规则冲突时，高优先级组覆盖低优先级组。

- **参数**：
  - `groupName` (`String`)：权限组名称。
  - `priority` (`Number`)：整数优先级。
- **返回**：`Boolean`。

### `getGroupPriority(groupName)`

获取权限组的优先级数值。

- **参数**：`groupName` (`String`)。
- **返回**：`Number`，组不存在时返回 `0`。

---

## 玩家权限管理与检查

### `addPlayerToGroup(playerUuid, groupName)`

将玩家**永久**加入指定权限组（如果玩家已在组中，会把到期时间更新为永久）。

- **参数**：
  - `playerUuid` (`String`)：玩家 UUID（在 LSE 中为 `player.uuid`）。
  - `groupName` (`String`)：目标权限组名称。
- **返回**：`Boolean`。

> 如需设置带过期时间的临时权限组，目前可通过游戏内命令 `/ba user add <玩家> <组名> <小时数>` 或 C++ API 完成。

### `removePlayerFromGroup(playerUuid, groupName)`

将玩家从指定权限组移除。

- **参数**：
  - `playerUuid` (`String`)：玩家 UUID。
  - `groupName` (`String`)：目标权限组名称。
- **返回**：`Boolean`。

### `getPlayerGroups(playerUuid)`

获取玩家当前有效的权限组名称列表。未显式加入任何有效组且启用了默认组时，会自动包含默认组。

- **参数**：`playerUuid` (`String`)。
- **返回**：`Array<String>`。

### `getPlayerGroupIds(playerUuid)`

获取玩家当前有效的权限组 ID 列表（同样包含默认组回退）。日常脚本开发通常优先使用 `getPlayerGroups`。

- **参数**：`playerUuid` (`String`)。
- **返回**：`Array<String>`。

### `getPlayersInGroup(groupName)`

获取显式加入指定权限组的玩家 UUID 列表（不包含仅通过默认组回退的玩家）。

- **参数**：`groupName` (`String`)。
- **返回**：`Array<String>`。

### `getAllPermissionsForPlayer(playerUuid)`

获取玩家合并后所有已编译规则的 `pattern` 列表。

- **参数**：`playerUuid` (`String`)。
- **返回**：`Array<String>`。
- **注意**：与 `getPermissionsOfGroup` 相同，返回的仅为规则模式字符串，不含允许/拒绝标记。判断玩家是否有权执行某操作请始终使用 `hasPermission`。

### `hasPermission(playerUuid, permissionNode)`

检查玩家是否拥有指定权限节点。会自动综合玩家所属组（或默认组）、继承链、优先级、通配符、反向拒绝规则以及节点默认值。

- **参数**：
  - `playerUuid` (`String`)：玩家 UUID。
  - `permissionNode` (`String`)：要检查的正向权限节点名称（如 `"essentials.command.home"`），**不要**加 `-` 前缀。
- **返回**：`Boolean`，有权限返回 `true`，无权限或被反向规则拒绝返回 `false`。
- **示例**：
  ```javascript
  const hasPermission = ll.import("BA", "hasPermission");

  if (hasPermission(player.uuid, "essentials.command.ban")) {
      // 允许执行
  } else {
      // 未授权或被 -essentials.command.ban 拒绝
  }
  ```
