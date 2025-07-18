# Bedrock Authority 权限管理插件使用文档

欢迎使用 Bedrock Authority，一个功能强大且灵活的 Minecraft Bedrock 服务器权限管理插件。本文档将指导您如何安装、配置和使用此插件来管理您服务器上的玩家权限。

## 目录
- [核心概念](#核心概念)
  - [权限节点 (Permission Nodes)](#权限节点-permission-nodes)
  - [权限组 (Permission Groups)](#权限组-permission-groups)
  - [继承 (Inheritance)](#继承-inheritance)
  - [优先级 (Priority)](#优先级-priority)
  - [临时权限 (Temporary Permissions)](#临时权限-temporary-permissions)
- [命令参考](#命令参考)
  - [别名](#别名)
  - [权限组管理](#权限组管理)
  - [权限节点管理](#权限节点管理)
  - [玩家权限管理](#玩家权限管理)
- [使用示例](#使用示例)
  - [场景：创建 VIP 权限组](#场景创建-vip-权限组)

---

## 核心概念

在开始使用命令之前，请先了解以下几个核心概念。

### 权限节点 (Permission Nodes)

权限节点是一个字符串，用于唯一标识一项操作或功能的权限，格式通常为 `插件名.功能.子功能`。例如 `myplugin.command.fly`。

- **通配符**: 您可以使用 `*` 作为通配符来匹配多个权限。例如，`myplugin.command.*` 会授予所有 `myplugin.command.` 开头的权限。
- **反向权限**: 您可以在权限节点前加上 `-` 来明确禁止某个权限。例如，一个组拥有 `myplugin.command.*` 但同时拥有 `-myplugin.command.admin`，那么该组的成员将无法使用 admin 命令。

### 权限组 (Permission Groups)

权限组是权限节点的集合。您可以创建一个权限组（例如 `vip`），向其添加多个权限节点，然后将玩家添加到该组中，玩家便会拥有该组的所有权限。

### 继承 (Inheritance)

继承允许一个权限组拥有另一个权限组的所有权限。例如，您可以创建一个 `admin` 组，并让它继承 `vip` 组。这样，`admin` 组的成员会自动拥有 `vip` 组的所有权限，同时您还可以为 `admin` 组添加额外的管理员权限。

### 优先级 (Priority)

当一个玩家属于多个权限组时，可能会出现权限冲突（例如，一个组授予 `a.b.c`，另一个组禁止 `-a.b.c`）。优先级就是用来解决这个问题的。

- 优先级是一个整数，**数字越大，优先级越高**。
- 当冲突发生时，**高优先级组的设定会覆盖低优先级组的设定**。

### 临时权限 (Temporary Permissions)

您可以将玩家临时添加到一个权限组中。当时限到达后，玩家将自动从该组中移除。这对于销售有时限的 VIP 或临时授权非常有用。

---

## 命令参考

### 别名

主命令为 `/bedrockauthority`，为了方便使用，插件内置了以下别名：
- `/ba`
- `/权限组`

下面所有命令都可以使用这些别名作为前缀，例如 `/ba 创建权限组 ...`。

### 权限组管理

| 命令 | 描述 |
| --- | --- |
| `/ba 创建权限组 <组名>` | 创建一个新的权限组。 |
| `/ba 删除权限组 <组名>` | 删除一个已存在的权限组。 |
| `/ba 设置权限组继承 <子组名> <父组名>` | 使一个组继承另一个组的权限。 |
| `/ba 移除权限组继承 <子组名> <父组名>` | 移除两个组之间的继承关系。 |

**示例:**
- 创建一个名为 `player` 的默认权限组：
  ```
  /ba 创建权限组 player
  ```
- 创建 `vip` 组并让它继承 `player` 组：
  ```
  /ba 创建权限组 vip
  /ba 设置权限组继承 vip player
  ```

### 权限节点管理

| 命令 | 描述 |
| --- | --- |
| `/ba 注册权限节点 <节点ID> [描述] [默认值]` | 注册一个新的权限节点，供插件开发者使用。 |
| `/ba 添加权限组节点 <组名> <节点ID>` | 为一个权限组添加一个权限节点。 |
| `/ba 删除权限组节点 <组名> <节点ID>` | 从一个权限组中移除一个权限节点。 |
| `/ba 列出权限组节点 <组名>` | 列出某个权限组拥有的所有权限节点（包括继承的）。 |

**示例:**
- 为 `vip` 组添加飞行权限：
  ```
  /ba 添加权限组节点 vip essentials.command.fly
  ```
- 禁止 `player` 组破坏方块：
  ```
  /ba 添加权限组节点 player -essentials.break
  ```

### 玩家权限管理

这些命令同时支持在线玩家（使用目标选择器 `@p`, `@a` 或玩家名）和离线玩家（直接输入玩家名）。

| 命令 | 描述 |
| --- | --- |
| `/ba 加入权限组 <玩家名> <组名> [时长(小时)]` | 将一个玩家添加到一个权限组，可选择性地设置一个以小时为单位的过期时间。 |
| `/ba 移除权限组 <玩家名> <组名>` | 将一个玩家从权限组中移除。 |
| `/ba 列出玩家权限组 <玩家名>` | 列出指定玩家所属的所有权限组。 |
| `/ba 列出玩家权限节点 <玩家名>` | 列出指定玩家当前生效的所有权限节点。 |

**示例:**
- 将玩家 `Steve` 添加到 `vip` 组：
  ```
  /ba 加入权限组 Steve vip
  ```
- 将玩家 `Alex` 添加到 `vip` 组，有效期为 30 天（720 小时）：
  ```
  /ba 加入权限组 Alex vip 720
  ```
- 查看 `Steve` 当前拥有的所有权限：
  ```
  /ba 列出玩家权限节点 Steve
  ```
- 将离线玩家 `Herobrine` 从 `admin` 组移除：
  ```
  /ba 移除权限组 Herobrine admin
  ```

---

## 使用示例

### 场景：创建 VIP 权限组

假设我们想创建一个 `VIP` 等级，拥有飞行和自定义称号的权限，并且有效期为 30 天。

1.  **创建默认玩家组和 VIP 组**
    首先，我们为所有普通玩家创建一个 `default` 组，并为 VIP 玩家创建 `vip` 组。
    ```
    /ba 创建权限组 default
    /ba 创建权限组 vip
    ```

2.  **设置继承关系**
    让 `vip` 组继承 `default` 组，这样 VIP 玩家也会拥有普通玩家的所有基础权限。
    ```
    /ba 设置权限组继承 vip default
    ```

3.  **为组分配权限**
    假设飞行权限的节点是 `essentials.fly`，自定义称号的权限是 `essentials.setnick`。
    ```
    /ba 添加权限组节点 vip essentials.fly
    /ba 添加权限组节点 vip essentials.setnick
    ```

4.  **将玩家添加到 VIP 组**
    现在，一个名叫 `Steve` 的玩家购买了 30 天的 VIP。
    30 天 = 720 小时。
    ```
    /ba 加入权限组 Steve vip 720
    ```
    `Steve` 现在就拥有了飞行和设置称号的权限。720 小时后，他将自动从 `vip` 组中移除。

5.  **验证权限**
    您可以随时检查玩家的权限组和最终生效的权限。
    ```
    /ba 列出玩家权限组 Steve
    /ba 列出玩家权限节点 Steve
    ```

希望这份文档能帮助您更好地管理您的服务器！
