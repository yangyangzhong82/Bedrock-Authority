# Bedrock Authority 使用手册

本手册面向服主和管理员，说明 Bedrock Authority 的权限判定规则、全部游戏内命令和常见配置场景。

## 目录

- [核心概念与判定规则](#核心概念与判定规则)
  - [权限节点、通配符与反向权限](#权限节点通配符与反向权限)
  - [权限组与继承](#权限组与继承)
  - [优先级与冲突判定顺序](#优先级与冲突判定顺序)
  - [默认权限组](#默认权限组)
  - [临时权限组](#临时权限组)
- [命令总览](#命令总览)
  - [命令前缀与执行权限](#命令前缀与执行权限)
  - [权限组管理命令](#权限组管理命令)
  - [权限节点命令](#权限节点命令)
  - [玩家权限命令](#玩家权限命令)
  - [检测与重载命令](#检测与重载命令)
- [常见使用场景](#常见使用场景)
  - [场景 1：搭建默认玩家组与 VIP 组](#场景-1搭建默认玩家组与-vip-组)
  - [场景 2：用优先级解决多组权限冲突](#场景-2用优先级解决多组权限冲突)
  - [场景 3：把部分管理命令下放给普通协管](#场景-3把部分管理命令下放给普通协管)

---

## 核心概念与判定规则

### 权限节点、通配符与反向权限

权限节点是用点号分隔的字符串，用来代表一项具体功能，例如 `essentials.command.fly`。

- **通配符 `*`**：匹配任意长度的任意字符。例如 `essentials.command.*` 可以匹配 `essentials.command.fly`、`essentials.command.home` 等；单独一个 `*` 匹配所有权限。
- **反向权限 `-`**：在节点前加 `-` 表示明确拒绝。例如给某个组同时配置 `essentials.command.*` 和 `-essentials.command.ban`，成员可以使用其他命令，但不能使用 `ban`。
- **大小写不敏感**：权限匹配不区分大小写，`MyPlugin.Fly` 和 `myplugin.fly` 视作同一节点。
- **节点默认值**：通过插件或命令注册的权限节点可以设置默认值（`true` 或 `false`）。没有配置任何匹配规则时，按注册默认值决定；未注册的节点默认拒绝（`false`）。

### 权限组与继承

权限组是权限规则的集合。把玩家放进组里，玩家就获得该组的所有规则。

- 一个组可以继承一个或多个父组，自动获得父组及其祖先组的全部权限规则。
- 系统会自动检测并拦截循环继承（例如 A 继承 B、B 再继承 A 会直接失败）。

### 优先级与冲突判定顺序

玩家同时属于多个组，或者一条权限同时被多条规则匹配时，系统按以下两步决定最终结果：

1. **同名规则合并（按组优先级）**：
   - 把玩家直接所属的组和所有继承的祖先组收集起来，按组的 `priority`（整数，越大越高）从低到高依次应用规则。
   - 如果低优先级组有 `a.b`，高优先级组有 `-a.b`，高优先级组会覆盖低优先级组。
2. **多条规则匹配（按规则长度）**：
   - 合并后的规则按模式字符串长度（不含 `-` 前缀）从长到短匹配，**越具体的规则越优先**。
   - 例如同时存在 `essentials.*`（允许）和 `-essentials.command.ban`（拒绝）时，检查 `essentials.command.ban` 会先命中更长的 `-essentials.command.ban`，结果为拒绝。
3. **无规则命中时回退默认值**：
   - 如果没有任何规则匹配，则使用该节点注册时的默认值；未注册则返回拒绝（`false`）。

> 小提示：当一个组通过通配符授予了一大片权限（如 `myplugin.*`），又想在同一个组或子组里禁掉其中某一项时，直接添加更具体的 `-myplugin.danger` 即可生效。

### 默认权限组

- 默认配置下，`config.json` 中的 `default_group` 为 `"default"`，模组启动时如果该组不存在会自动创建。
- **触发条件**：只有当玩家**没有显式加入任何有效权限组**（包括从未入组，或临时组全部过期）时，才会自动应用默认组及其继承链上的权限。
- 一旦玩家被显式加入了其他组（例如 `vip`），就不再自动附加默认组。因此如果希望 `vip` 也拥有普通玩家的基础权限，请让 `vip` 继承 `default`：
  ```text
  /ba group inherit vip default
  ```
- 如果不需要默认组，可执行 `/ba group cleardefault`，或在 `config.json` 中将 `default_group` 留空 `""`。

### 临时权限组

- 把玩家加入组时可以传入时长（单位：小时，支持小数，例如 `0.5` 表示 30 分钟）。
- 留空或填 `0` 表示永久成员；填写大于 `0` 的时长时，至少需要 1 秒。
- 如果玩家已经在该组中，再次执行入组命令会**更新**其过期时间。
- 到达过期时间后，权限检查会立刻按过期处理（无需等待后台清理）；后台清理任务只负责按 `cleanup_interval_seconds` 定期从数据库删除过期记录并触发离组事件。

---

## 命令总览

### 命令前缀与执行权限

主命令为 `/bedrockauthority`，支持两个简写别名，三者完全等价：

- `/bedrockauthority`
- `/ba`
- `/权限组`

每条子命令都同时支持**英文路径**和**中文指令**两种写法。

**谁能执行命令？**
- 控制台和服务器 OP（`GameDirectors` 及以上权限等级）默认拥有全部管理命令权限。
- 普通玩家也可以执行单条管理命令，前提是为其授予下表对应的 `bedrockauthority.admin.*` 权限节点（这些节点会在模组启动时自动注册，默认值为 `false`）。

**玩家参数说明：**
- 涉及 `<玩家>` 的命令既支持在线玩家选择器（如 `@p`、`@a`）和在线玩家名，也支持曾经进过服的离线玩家名（通过 LeviLamina `PlayerInfo` 解析 UUID）。

---

### 权限组管理命令

| 英文命令 | 中文命令 | 所需权限节点 | 说明 |
| --- | --- | --- | --- |
| `/ba group create <组名>` | `/ba 创建权限组 <组名>` | `bedrockauthority.admin.group.create` | 创建新权限组（默认优先级为 `0`）。 |
| `/ba group delete <组名>` | `/ba 删除权限组 <组名>` | `bedrockauthority.admin.group.delete` | 删除已存在的权限组及其关联数据。 |
| `/ba group list` | `/ba 列出所有权限组` | `bedrockauthority.admin.group.list` | 列出当前所有权限组名称。 |
| `/ba group priority <组名> <整数>` | `/ba 设置组优先级 <组名> <整数>` | `bedrockauthority.admin.group.priority` | 设置权限组优先级，数字越大优先级越高。 |
| `/ba group description <组名> <描述>` | `/ba 修改组描述 <组名> <描述>` | `bedrockauthority.admin.group.description` | 修改权限组的文字描述。 |
| `/ba group nodes <组名>` | `/ba 列出权限组节点 <组名>` | `bedrockauthority.admin.group.nodes` | 列出该组最终生效的所有规则（含继承和 `-` 拒绝规则）。 |
| `/ba group grant <组名> <节点>` | `/ba 添加权限组节点 <组名> <节点>` | `bedrockauthority.admin.group.grant` | 给组添加一条规则（可以是普通节点、通配符或 `-` 反向规则）。 |
| `/ba group revoke <组名> <节点>` | `/ba 删除权限组节点 <组名> <节点>` | `bedrockauthority.admin.group.revoke` | 从组中删除一条已添加的直接规则（删除反向规则时需带 `-`）。 |
| `/ba group inherit <子组> <父组>` | `/ba 设置权限组继承 <子组> <父组>` | `bedrockauthority.admin.group.inherit` | 让 `<子组>` 继承 `<父组>` 的全部权限。 |
| `/ba group uninherit <子组> <父组>` | `/ba 移除权限组继承 <子组> <父组>` | `bedrockauthority.admin.group.uninherit` | 取消 `<子组>` 对 `<父组>` 的直接继承关系。 |
| `/ba group default` | `/ba 查看默认权限组` | `bedrockauthority.admin.group.default` | 查看当前生效的默认权限组名称。 |
| `/ba group default <组名>` | `/ba 设置默认权限组 <组名>` | `bedrockauthority.admin.group.default` | 将指定已存在的组设为默认权限组，并写入 `config.json`。 |
| `/ba group cleardefault` | `/ba 清除默认权限组` | `bedrockauthority.admin.group.cleardefault` | 禁用默认权限组，并写入 `config.json`。 |

> 注意：`group grant` 添加权限规则时，**不要求**节点先注册，可直接添加任意节点、通配符（如 `shop.*`）或反向规则（如 `-shop.admin`）。

---

### 权限节点命令

| 英文命令 | 中文命令 | 所需权限节点 | 说明 |
| --- | --- | --- | --- |
| `/ba node register <节点> [描述] [默认值]` | `/ba 注册权限节点 <节点> [描述] [默认值]` | `bedrockauthority.admin.node.register` | 注册或更新权限节点的描述与默认值（`true`/`false`，默认 `false`），同时刷新命令自动补全列表。 |

---

### 玩家权限命令

| 英文命令 | 中文命令 | 所需权限节点 | 说明 |
| --- | --- | --- | --- |
| `/ba user add <玩家> <组名> [小时数]` | `/ba 加入权限组 <玩家> <组名> [小时数]` | `bedrockauthority.admin.user.add` | 将玩家加入权限组；不填或填 `0` 为永久，正数（可小数）为临时时长。再次执行可更新过期时间。 |
| `/ba user remove <玩家> <组名>` | `/ba 移除权限组 <玩家> <组名>` | `bedrockauthority.admin.user.remove` | 将玩家从指定权限组中移除。 |
| `/ba user groups <玩家>` | `/ba 列出玩家权限组 <玩家>` | `bedrockauthority.admin.user.groups` | 查看玩家当前所属的所有权限组（无显式组时会显示默认组）。 |
| `/ba user nodes <玩家>` | `/ba 列出玩家权限节点 <玩家>` | `bedrockauthority.admin.user.nodes` | 列出玩家当前生效的**允许**权限规则。 |
| `/ba user expiry <玩家> <组名>` | `/ba 查看玩家组剩余时间 <玩家> <组名>` | `bedrockauthority.admin.user.expiry` | 查看玩家在指定组的剩余秒数，或是否为永久成员。 |

---

### 检测与重载命令

| 英文命令 | 中文命令 | 所需权限节点 | 说明 |
| --- | --- | --- | --- |
| `/ba check <玩家> <节点>` | `/ba 检查玩家权限 <玩家> <节点>` | `bedrockauthority.admin.check` | 检查指定玩家当前是否拥有某个权限节点。 |
| `/ba reload` | `/ba 重载` | `bedrockauthority.admin.reload` | 重载内存缓存以及 `config.json` 中的 `enable_cache_warmup`、`cleanup_interval_seconds`、`default_group`（数据库连接修改需重启服务器）。 |

---

## 常见使用场景

### 场景 1：搭建默认玩家组与 VIP 组

目标：普通玩家默认可以使用基础命令；VIP 拥有普通玩家的全部权限，额外解锁飞行和称号，有效期 30 天。

```text
# 1. 给默认组 default（启动时已自动创建）配置基础权限
/ba group grant default essentials.home
/ba group grant default essentials.tpa

# 2. 创建 vip 组，设置更高优先级并继承 default
/ba group create vip
/ba group priority vip 10
/ba group inherit vip default

# 3. 给 vip 组添加专属权限
/ba group grant vip essentials.fly
/ba group grant vip essentials.nick

# 4. 给玩家 Steve 开通 30 天 VIP（30 × 24 = 720 小时）
/ba user add Steve vip 720

# 5. 检查效果与剩余时间
/ba check Steve essentials.fly
/ba user expiry Steve vip
```

720 小时后，`Steve` 会自动退出 `vip` 组并回退到 `default` 组。

### 场景 2：用优先级解决多组权限冲突

目标：创建一个禁言/惩罚组 `muted`，即使玩家同时是 `vip`，只要加入 `muted` 就必须禁止聊天权限 `chat.send`。

```text
# 1. vip 组优先级为 10，拥有 chat.send
/ba group grant vip chat.send

# 2. 创建 muted 组，优先级设得比 vip 更高（例如 100），并添加反向权限
/ba group create muted
/ba group priority muted 100
/ba group grant muted -chat.send

# 3. 将违规玩家临时加入 muted 组 2 小时
/ba user add Alex muted 2

# 4. 验证：因为 muted 优先级高于 vip，结果为“没有权限”
/ba check Alex chat.send
```

### 场景 3：把部分管理命令下放给普通协管

目标：让协管组 `helper` 可以查看权限、给玩家加减组，但不能删除权限组或重载配置。

```text
/ba group create helper
/ba group inherit helper default
/ba group grant helper bedrockauthority.admin.check
/ba group grant helper bedrockauthority.admin.user.groups
/ba group grant helper bedrockauthority.admin.user.add
/ba group grant helper bedrockauthority.admin.user.remove
/ba user add ModPlayer helper
```
