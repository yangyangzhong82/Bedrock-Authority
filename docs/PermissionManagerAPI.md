# Bedrock Authority C++ API 文档

本文档面向 LeviLamina C++ 模组开发者，说明如何接入 Bedrock Authority（BA）进行权限注册、组管理、权限检查以及监听权限变更事件。

## 目录

- [接入准备](#接入准备)
- [获取实例与生命周期](#获取实例与生命周期)
- [权限判定规则](#权限判定规则)
- [API 参考](#api-参考)
  - [权限节点管理](#权限节点管理)
  - [权限组管理](#权限组管理)
  - [组内权限管理](#组内权限管理)
  - [组继承管理](#组继承管理)
  - [组优先级管理](#组优先级管理)
  - [默认权限组](#默认权限组)
  - [玩家与组关系管理](#玩家与组关系管理)
  - [权限检查与过期清理](#权限检查与过期清理)
- [核心数据结构](#核心数据结构)
- [权限变更事件](#权限变更事件)
- [完整示例](#完整示例)

---

## 接入准备

构建 Bedrock Authority 后，开发文件位于 `bin/` 目录：

- 头文件：`bin/include/Bedrock-Authority/permission/`
  - 主接口：`permission/PermissionManager.h`
  - 数据结构：`permission/PermissionData.h`
  - 变更事件：`permission/events/*.h`
- 导入库：`bin/lib/Bedrock-Authority.lib`

在你的模组 `manifest.json` 中声明对 `Bedrock-Authority` 的依赖，确保 BA 先于你的模组完成加载。

## 获取实例与生命周期

`BA::permission::PermissionManager` 是全局单例，通过 `getInstance()` 获取引用：

```cpp
#include "permission/PermissionManager.h"

auto& pm = BA::permission::PermissionManager::getInstance();
```

`init()`、`shutdown()` 与 `reloadCache()` 由 BA 自身维护，外部插件一般不需要手动调用：

- `bool init(db::IDatabase* db, bool enableWarmup = true, unsigned int threadPoolSize = 4)`：初始化数据库与缓存。`threadPoolSize` 仅为兼容 ABI 保留，当前版本采用同步缓存失效与版本校验。
- `void shutdown()`：关闭管理器。
- `void reloadCache(bool warmup)`：重新加载继承图与默认权限索引，清空派生缓存；`warmup` 为 `true` 时执行全量预热。

> 请确保在 BA 完成 `load` 之后再调用任何读写或检查接口（例如在你模组的 `enable()` 或业务回调中调用）。

---

## 权限判定规则

调用 `hasPermission(playerUuid, permissionNode)` 时，按以下顺序判定：

1. **确定玩家有效组**：取玩家所有未过期的显式分组；如果玩家没有任何有效显式分组，则回退到当前配置的默认权限组。
2. **展开继承并按优先级合并**：收集所有有效组及其全部祖先组，按组 `priority` 从低到高应用规则。同一 `pattern` 下，高优先级组的规则覆盖低优先级组。
3. **按规则长度匹配（不区分大小写）**：将合并后的规则按 `pattern` 长度从长到短匹配，`*` 匹配任意字符序列。先命中更具体的规则；反向规则（添加时以 `-` 开头）的 `state` 为 `false`。
4. **回退节点默认值**：若无任何规则命中，则返回该节点注册时的 `defaultValue`；未注册则返回 `false`。

---

## API 参考

所有方法均位于 `BA::permission::PermissionManager` 类中。

### 权限节点管理

| 方法 | 说明 |
| --- | --- |
| `bool registerPermission(const std::string& name, const std::string& description = "", bool defaultValue = false)` | 注册或更新一个权限节点（存在时更新描述和默认值），并刷新权限默认值缓存。 |
| `bool permissionExists(const std::string& name)` | 检查权限节点是否已注册。 |
| `std::vector<std::string> getAllPermissions()` | 获取所有已注册的权限节点名称。 |

### 权限组管理

| 方法 | 说明 |
| --- | --- |
| `bool createGroup(const std::string& groupName, const std::string& description = "")` | 创建权限组（默认优先级为 `0`）。若同名组已存在或创建事件被取消，返回 `false`。 |
| `bool deleteGroup(const std::string& groupName)` | 删除权限组及其关联的权限、继承与成员记录。 |
| `bool groupExists(const std::string& groupName)` | 检查权限组是否存在。 |
| `std::vector<std::string> getAllGroups()` | 获取所有权限组的名称列表。 |
| `GroupDetails getGroupDetails(const std::string& groupName)` | 获取权限组详细信息。组不存在时返回的 `isValid` 为 `false`。 |
| `bool updateGroupDescription(const std::string& groupName, const std::string& newDescription)` | 更新权限组描述。 |
| `std::string getGroupDescription(const std::string& groupName)` | 获取权限组描述；组不存在时返回空字符串。 |

### 组内权限管理

> 添加规则时不要求节点提前注册，支持通配符（如 `myplugin.*`）和反向规则（如 `-myplugin.ban`）。移除反向规则时同样需要带 `-` 前缀。

| 方法 | 说明 |
| --- | --- |
| `bool addPermissionToGroup(const std::string& groupName, const std::string& permissionName)` | 向组添加一条权限规则。组不存在、规则已存在或事件被取消时返回 `false`。 |
| `bool removePermissionFromGroup(const std::string& groupName, const std::string& permissionName)` | 从组移除一条直接权限规则。 |
| `std::vector<std::string> getDirectPermissionsOfGroup(const std::string& groupName)` | 获取组**直接配置**的原始规则字符串列表（不含继承）。 |
| `std::vector<CompiledPermissionRule> getPermissionsOfGroup(const std::string& groupName)` | 获取组**最终生效**的已编译规则（含继承与优先级合并结果）。 |
| `size_t addPermissionsToGroup(const std::string& groupName, const std::vector<std::string>& permissionRules)` | 批量向组添加权限规则，返回实际成功添加的条数。 |
| `size_t removePermissionsFromGroup(const std::string& groupName, const std::vector<std::string>& permissionRules)` | 批量从组移除权限规则，返回实际成功移除的条数。 |

### 组继承管理

| 方法 | 说明 |
| --- | --- |
| `bool addGroupInheritance(const std::string& groupName, const std::string& parentGroupName)` | 让 `groupName` 继承 `parentGroupName`。任一组不存在、关系已存在、会形成循环继承或事件被取消时返回 `false`。 |
| `bool removeGroupInheritance(const std::string& groupName, const std::string& parentGroupName)` | 移除直接继承关系。 |
| `std::vector<std::string> getDirectParentGroups(const std::string& groupName)` | 获取直接父组名称列表。 |
| `std::vector<std::string> getAllAncestorGroups(const std::string& groupName)` | 获取所有祖先组名称列表（含间接继承，**不包含自身**）。 |

### 组优先级管理

| 方法 | 说明 |
| --- | --- |
| `bool setGroupPriority(const std::string& groupName, int priority)` | 设置组优先级，数值越大优先级越高。 |
| `int getGroupPriority(const std::string& groupName)` | 获取组优先级；组不存在时返回 `0`。 |

### 默认权限组

| 方法 | 说明 |
| --- | --- |
| `bool setDefaultGroup(const std::string& groupName)` | 设置当前运行时的默认权限组。传入空字符串 `""` 表示禁用；传入不存在的非空组名返回 `false`。此方法**不修改** `config.json`。 |
| `std::string getDefaultGroup()` | 获取当前默认权限组名称；未启用时返回空字符串。 |

### 玩家与组关系管理

| 方法 | 说明 |
| --- | --- |
| `bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName)` | 将玩家永久加入权限组（若已在组中，会将到期时间更新为永久）。 |
| `bool addPlayerToGroup(const std::string& playerUuid, const std::string& groupName, long long durationSeconds)` | 将玩家加入权限组，`durationSeconds > 0` 表示从当前时间起持续的秒数，`<= 0` 表示永久。若已在组中会覆盖原到期时间。 |
| `bool removePlayerFromGroup(const std::string& playerUuid, const std::string& groupName)` | 将玩家从权限组移除。 |
| `std::vector<std::string> getPlayerGroups(const std::string& playerUuid)` | 获取玩家有效权限组名称列表（无显式组时自动包含默认组）。 |
| `std::vector<std::string> getPlayerGroupIds(const std::string& playerUuid)` | 获取玩家有效权限组 ID 列表（无显式组时自动包含默认组）。 |
| `std::vector<GroupDetails> getPlayerGroupsWithPriorities(const std::string& playerUuid)` | 获取玩家有效权限组详情，包含 `priority` 和 `expirationTime`（无显式组时自动包含默认组）。 |
| `std::vector<std::string> getPlayersInGroup(const std::string& groupName)` | 获取显式加入指定权限组的玩家 UUID 列表（不包含仅通过默认组回退的玩家）。 |
| `size_t addPlayerToGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames)` | 批量将玩家永久加入多个组，返回实际新加入的组数量。**已在组内的成员会保留原有到期时间。** |
| `size_t removePlayerFromGroups(const std::string& playerUuid, const std::vector<std::string>& groupNames)` | 批量将玩家从多个组移除，返回实际移除的组数量。 |
| `std::optional<long long> getPlayerGroupExpirationTime(const std::string& playerUuid, const std::string& groupName)` | 获取玩家在指定组的到期时间（Unix 秒时间戳）。永久成员或不在组内时返回 `std::nullopt`。 |
| `bool setPlayerGroupExpirationTime(const std::string& playerUuid, const std::string& groupName, long long durationSeconds)` | 更新已在组内玩家的到期时间（`<= 0` 表示改为永久）。**不会**自动添加不在组内的玩家，玩家不在组中时返回 `false`。 |

### 权限检查与过期清理

| 方法 | 说明 |
| --- | --- |
| `bool hasPermission(const std::string& playerUuid, const std::string& permissionNode)` | 检查玩家是否拥有目标权限节点。请传入正向节点名（如 `"myplugin.fly"`），不要带 `-` 前缀。 |
| `std::vector<CompiledPermissionRule> getAllPermissionsForPlayer(const std::string& playerUuid)` | 获取玩家最终生效的已编译权限规则列表（按 `pattern` 长度降序排列）。 |
| `void runPeriodicCleanup()` | 立即执行一次过期成员清理，删除已到期的成员记录并发布离组事件。 |

---

## 核心数据结构

定义在 `permission/PermissionData.h`，位于 `BA::permission` 命名空间：

```cpp
struct GroupDetails {
    std::string              id;
    std::string              name;
    std::string              description;
    int                      priority;
    bool                     isValid = false;          // 查询不到组时为 false
    std::optional<long long> expirationTime;           // Unix 秒时间戳，永久或非成员查询时为 std::nullopt
};

struct CompiledPermissionRule {
    std::string pattern; // 去掉 '-' 前缀后的规则模式
    std::regex  regex;   // 编译后的不区分大小写正则
    bool        state;   // true = 允许，false = 拒绝
};
```

---

## 权限变更事件

所有事件位于 `BA::permission::event` 命名空间，通过 LeviLamina 的 `ll::event::EventBus` 发布。

每项变更操作都对应一对事件：
- **`*BeforeEvent`**：继承自 `ll::event::Cancellable<ll::event::Event>`。监听器可以调用 `cancel()` 阻止操作，也可以直接修改事件里的参数引用；BA 会按修改后的参数执行写库、失效缓存并发布 After 事件。
- **`*AfterEvent`**：继承自 `ll::event::Event`，仅在写库提交成功后发布，参数为最终生效的值（只读引用）。

> 事件对象中保存的都是引用，**仅在同步回调执行期间有效**；如果需要在异步任务中使用，请先拷贝值。

| 操作 | 事件名前缀 | 头文件（`permission/events/`） | 访问字段 / 方法 |
| --- | --- | --- | --- |
| 玩家入组 | `PlayerJoinGroup` | `PlayerJoinGroupEvent.h` | `getPlayerUuid()`、`getGroupName()`、`getExpirationTime()`（Unix 秒时间戳，`std::nullopt` 为永久） |
| 玩家离组 | `PlayerLeaveGroup` | `PlayerLeaveGroupEvent.h` | `getPlayerUuid()`、`getGroupName()` |
| 增删组权限规则 | `GroupPermissionChange` | `GroupPermissionChangeEvent.h` | `getGroupName()`、`getPermissionRule()`、`isAdd()` |
| 创建权限组 | `GroupCreate` | `GroupManagementEvents.h` | 公开引用成员：`groupName`、`description` |
| 删除权限组 | `GroupDelete` | `GroupManagementEvents.h` | 公开引用成员：`groupName` |
| 修改继承关系 | `GroupInheritanceChange` | `GroupManagementEvents.h` | 公开引用成员：`groupName`、`parentGroupName`、`adding` |
| 修改组优先级 | `GroupPriorityChange` | `GroupManagementEvents.h` | 公开引用成员：`groupName`、`priority` |
| 修改组描述 | `GroupDescriptionChange` | `GroupManagementEvents.h` | 公开引用成员：`groupName`、`description` |

**批量操作与定时清理的事件行为：**
- `addPlayerToGroups` / `removePlayerFromGroups` 会逐项发布 Before 事件；某一项被取消不会中断其余项，数据库事务提交后再逐项发布 After 事件。
- 定时清理（`CleanupScheduler`）在后台清理线程中同步发布 `PlayerLeaveGroupBeforeEvent` / `PlayerLeaveGroupAfterEvent`。如果在 Before 中取消清理，该记录会留在数据库中等待下次周期重试，但权限检查仍会按到期时间视为已过期。

---

## 完整示例

### 注册并检查权限

```cpp
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "permission/PermissionManager.h"

void onModEnable() {
    auto& pm = BA::permission::PermissionManager::getInstance();
    // 注册权限节点，默认不允许
    pm.registerPermission("myplugin.fly", "允许使用飞行功能", false);
}

void onFlyCommand(Player& player) {
    auto& pm = BA::permission::PermissionManager::getInstance();
    if (!pm.hasPermission(player.getUuid().asString(), "myplugin.fly")) {
        player.sendMessage("你没有权限使用此功能。");
        return;
    }
    player.sendMessage("已开启飞行。");
}
```

### 监听权限变更事件

```cpp
#include "ll/api/event/EventBus.h"
#include "permission/events/GroupManagementEvents.h"
#include "permission/events/PlayerJoinGroupEvent.h"

void registerListeners() {
    auto& bus = ll::event::EventBus::getInstance();

    // 限制组优先级不得超过 1000
    bus.emplaceListener<BA::permission::event::GroupPriorityChangeBeforeEvent>(
        [](BA::permission::event::GroupPriorityChangeBeforeEvent& ev) {
            if (ev.priority > 1000) {
                ev.priority = 1000;
            }
        }
    );

    // 玩家加入 vip 组后执行自定义通知
    bus.emplaceListener<BA::permission::event::PlayerJoinGroupAfterEvent>(
        [](BA::permission::event::PlayerJoinGroupAfterEvent& ev) {
            if (ev.getGroupName() == "vip") {
                // 此处可直接读取 ev.getPlayerUuid() 和 ev.getExpirationTime()
            }
        }
    );
}
```
