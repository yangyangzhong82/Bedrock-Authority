# Bedrock Authority API 使用文档

本文档为插件开发者提供指导，说明如何使用 Bedrock Authority (BA) 的 C++ API 来在您的插件中实现与权限系统的交互。

## 目录
- [获取 PermissionManager 实例](#获取-permissionmanager-实例)
- [核心生命周期](#核心生命周期)
- [API 参考](#api-参考)
  - [权限节点管理](#权限节点管理)
  - [权限组管理](#权限组管理)
  - [组内权限管理](#组内权限管理)
  - [组继承与优先级](#组继承与优先级)
  - [玩家与组关系管理](#玩家与组关系管理)
  - [权限检查](#权限检查)
- [数据结构](#数据结构)
- [使用示例](#使用示例)

---

## 获取 PermissionManager 实例

`PermissionManager` 是一个单例类，您可以通过 `getInstance()` 方法在代码的任何位置获取其唯一实例。

```cpp
#include "permission/PermissionManager.h"

// 获取 PermissionManager 实例
auto& pm = BA::permission::PermissionManager::getInstance();
```

**重要提示**: 在调用任何其他 API 方法之前，请确保 Bedrock Authority 插件已经加载并完成了初始化。您可以在您的插件的 `onEnable` 或 `onLoad` 阶段获取实例，但在调用具体功能（如权限检查）时，BA 插件必须是激活状态。

## 核心生命周期

通常，您不需要手动管理 BA 的生命周期。`init` 和 `shutdown` 方法由 BA 插件自身在启动和关闭时调用。

- `bool init(db::IDatabase* db, bool enableWarmup = true, unsigned int threadPoolSize = 4)`: 初始化管理器。
- `void shutdown()`: 关闭管理器并释放资源。

---

## API 参考

### 权限节点管理

用于注册和查询在服务器上可用的权限节点。

- `bool registerPermission(const std::string& name, const std::string& description = "", bool defaultValue = false)`
  **描述**: 注册一个权限节点。如果插件需要检查某个特定权限，应首先注册它。
  **参数**:
    - `name`: 权限节点名称 (e.g., `myplugin.feature.use`)。
    - `description`: 权限的简要描述。
    - `defaultValue`: 当玩家没有任何特定规则时，此权限的默认状态 (`true` 或 `false`)。
  **返回**: `true` 如果注册成功。

- `bool permissionExists(const std::string& name)`
  **描述**: 检查一个权限节点是否已被注册。

- `std::vector<std::string> getAllPermissions()`
  **描述**: 获取所有已注册的权限节点的名称列表。

### 权限组管理

用于创建、删除和查询权限组。

- `bool createGroup(const std::string& groupName, const std::string& description = "")`
  **描述**: 创建一个新的权限组。

- `bool deleteGroup(const std::string& groupName)`
  **描述**: 删除一个已存在的权限组。

- `bool groupExists(const std::string& groupName)`
  **描述**: 检查一个权限组是否存在。

- `std::vector<std::string> getAllGroups()`
  **描述**: 获取所有权限组的名称列表。

- `GroupDetails getGroupDetails(const std::string& groupName)`
  **描述**: 获取一个组的详细信息，包括 ID, 名称, 描述和优先级。

### 组内权限管理

用于管理特定权限组拥有的权限节点。

- `bool addPermissionToGroup(const std::string& groupName, const std::string& permissionName)`
  **描述**: 向组中添加一个权限节点。`permissionName` 可以是普通节点，也可以是带 `-` 前缀的反向权限。

- `bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionName)`
  **描述**: 从组中移除一个权限节点。

- `std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName)`
  **描述**: 获取一个组**直接拥有**的权限节点列表（不包括继承的）。

- `std::vector<CompiledPermissionRule> getPermissionsOfGroup(const std::string& groupName)`
  **描述**: 获取一个组**最终生效**的所有权限规则（包括继承的），已编译为正则表达式，性能更高。

### 组继承与优先级

- `bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName)`
  **描述**: 使 `groupName` 组继承 `parentGroupName` 组。

- `bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName)`
  **描述**: 移除继承关系。

- `bool setGroupPriority(const std::string& groupName, int priority)`
  **描述**: 设置组的优先级。数字越大，优先级越高。

- `int getGroupPriority(const std::string& groupName)`
  **描述**: 获取组的优先级。

### 默认权限组

- `bool setDefaultGroup(const std::string& groupName)`
  **描述**: 设置默认权限组。未显式分配任何组的玩家将自动拥有该组的权限，无需写入 `player_groups` 表。传入空字符串表示禁用默认权限组；如果指定的非空组不存在则返回 `false`。

- `std::string getDefaultGroup()`
  **描述**: 获取当前配置的默认权限组名称。如果未设置则返回空字符串。

### 玩家与组关系管理

- `bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName)`
  **描述**: 将一个玩家（通过 UUID）永久添加到一个组。

- `bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName, long long durationSeconds)`
  **描述**: 将一个玩家临时添加到一个组，`durationSeconds` 为持续的秒数。

- `bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName)`
  **描述**: 从组中移除一个玩家。

- `std::vector<std::string> getPlayerGroups(const std::string& playerUuid)`
  **描述**: 获取一个玩家所属的所有组的名称列表。

- `std::optional<long long> getPlayerGroupExpirationTime(const std::string& playerUuid, const std::string& groupName)`
  **描述**: 获取玩家在某个组内的过期时间（Unix 时间戳，秒）。如果永久或玩家不在组内，返回 `std::nullopt`。

### 权限检查

这是最常用的功能，用于在代码中检查玩家是否拥有执行某项操作的权限。

- `bool hasPermission(const std::string& playerUuid, const std::string& permissionNode)`
  **描述**: 检查指定玩家是否拥有某个权限节点。这是进行权限验证的核心函数。
  **参数**:
    - `playerUuid`: 玩家的 UUID 字符串。
    - `permissionNode`: 要检查的权限节点 (e.g., `myplugin.feature.use`)。
  **返回**: `true` 如果玩家拥有该权限，否则返回 `false`。

- `std::vector<CompiledPermissionRule> getAllPermissionsForPlayer(const std::string& playerUuid)`
  **描述**: 获取一个玩家最终生效的所有权限规则。主要用于调试或需要复杂逻辑的场景。

---

## 数据结构

- `struct GroupDetails`: 包含组的 `id`, `name`, `description`, `priority`, `isValid`, 和 `expirationTime`。
- `struct CompiledPermissionRule`: 包含 `pattern` (原始字符串), `regex` (编译后的正则表达式), 和 `state` (true/false)。

---

## 权限变更事件

在 `BA::permission::event` 命名空间中，每项操作都有可取消的 `BeforeEvent` 和提交后发布的 `AfterEvent`。监听器可以修改 Before 参数，管理器按修改后的玩家、组、规则、操作方向和到期时间执行，成功后按实际目标失效缓存并发布 After。事件中的引用仅在同步回调期间有效。

| 操作 | 事件名前缀 | 头文件 |
| --- | --- | --- |
| 玩家入组、离组 | `PlayerJoinGroup`、`PlayerLeaveGroup` | `PlayerJoinGroupEvent.h`、`PlayerLeaveGroupEvent.h` |
| 添加、移除组权限规则 | `GroupPermissionChange` | `GroupPermissionChangeEvent.h` |
| 创建、删除组 | `GroupCreate`、`GroupDelete` | `GroupManagementEvents.h` |
| 修改继承、优先级、描述 | `GroupInheritanceChange`、`GroupPriorityChange`、`GroupDescriptionChange` | `GroupManagementEvents.h` |

头文件位于 `permission/events/`。例如，在 `GroupPriorityChangeBeforeEvent` 回调中给 `event.priority` 赋值即可修改将要保存的优先级。玩家入组事件的 `getExpirationTime()` 为 Unix 秒时间戳，`std::nullopt` 表示永久。

批量成员操作逐项发布 Before，取消某一项后继续处理其余项，事务提交后逐项发布 After。已在组内的玩家通过批量入组会保留原到期时间。定期清理在清理线程同步发布离组事件，删除时会重新检查目标是否过期。取消清理会保留记录供后续周期重试，已到期的成员资格仍按到期时间失效。

## 使用示例

假设您的插件有一个 `/fly` 命令，需要检查玩家是否拥有 `myplugin.fly` 权限。

1.  **在插件启动时注册权限**

    ```cpp
    // In your plugin's onEnable() or similar
    #include "permission/PermissionManager.h"

    void MyPlugin::onEnable() {
        auto& pm = BA::permission::PermissionManager::getInstance();
        // 注册权限，默认不允许
        pm.registerPermission("myplugin.fly", "Allows the player to use the /fly command.", false);
    }
    ```

2.  **在命令执行时检查权限**

    ```cpp
    #include "permission/PermissionManager.h"
    #include "mc/world/actor/player/Player.h"
    #include "mc/platform/UUID.h"

    void onFlyCommand(Player* player) {
        auto& pm = BA::permission::PermissionManager::getInstance();
        std::string uuidStr = player->getUuid().asString();

        if (pm.hasPermission(uuidStr, "myplugin.fly")) {
            // 玩家有权限，执行飞行逻辑
            player->sendText("飞行功能已开启！");
            // ...
        } else {
            // 玩家没有权限
            player->sendText("你没有权限使用此命令。");
        }
    }
    ```

通过以上 API，您可以轻松地将您的插件与 Bedrock Authority 权限系统集成，实现精细化的权限控制。
