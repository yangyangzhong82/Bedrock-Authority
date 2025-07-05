# Bedrock Authority - 全能权限管理插件

**Bedrock Authority** 是一个为 Minecraft: Bedrock Edition (BDS) 设计的、功能强大且高度可扩展的权限管理插件。它提供了一套完整的工具，用于精细化控制您服务器上的玩家权限，支持多种数据库后端，并包含一个直观的 Web UI，让权限管理变得前所未有的简单。

![GitHub Repo stars](https://img.shields.io/github/stars/yangyangzhong82/Bedrock-Authority?style=social)
![GitHub license](https://img.shields.io/github/license/yangyangzhong82/Bedrock-Authority)
![GitHub issues](https://img.shields.io/github/issues/yangyangzhong82/Bedrock-Authority)

## ✨ 核心功能

*   **强大的权限系统**: 支持通配符 (`*`) 和反向权限 (`-`)，实现灵活的权限控制。
*   **权限组与继承**: 通过创建权限组并设置继承关系，轻松构建分层的权限结构。
*   **优先级系统**: 解决多权限组之间的权限冲突，高优先级组的设置将覆盖低优先级组。
*   **临时权限**: 可为玩家设置有时间限制的权限组，到期后自动移除。
*   **多数据库支持**: 支持 **SQLite**, **MySQL**, 和 **PostgreSQL**，满足不同规模服务器的需求。
*   **高性能缓存**: 内置高效的缓存系统，并采用异步更新策略，最大程度减少对服务器性能的影响。
*   **Web 管理界面**: 提供一个直观的网页界面，方便您查看和管理权限组、玩家和权限。
*   **丰富的游戏内命令**: 提供一套完整的命令，方便管理员在游戏内直接管理权限。
*   **开发者 API**: 提供 C++ API (`PermissionManagerAPI.md`) 和脚本 API (`ScriptAPI.md`)，方便其他插件进行集成。

## 💿 安装

1.  从发布页面下载最新的 `Bedrock-Authority.zip` 文件。
2.  解压文件，并将 `Bedrock-Authority` 文件夹放入您服务器的 `plugins` 目录下。
3.  启动服务器，插件会自动生成默认的配置文件 `plugins/Bedrock-Authority/config.json`。
4.  根据您的需求修改配置文件，然后重启服务器使配置生效。

## ⚙️ 配置

插件的配置文件位于 `plugins/Bedrock-Authority/config.json`。以下是所有配置项的详细说明：

```json
{
    "version": 2,
    "db_type": "sqlite",
    "sqlite_path": "ba.db",
    "mysql_host": "127.0.0.1",
    "mysql_user": "root",
    "mysql_password": "",
    "mysql_db": "ba",
    "mysql_port": 3306,
    "postgresql_host": "127.0.0.1",
    "postgresql_user": "postgres",
    "postgresql_password": "",
    "postgresql_db": "ba",
    "postgresql_port": 5432,
    "http_server_enabled": true,
    "http_server_host": "0.0.0.0",
    "http_server_port": 8080,
    "http_server_static_path": "http_static",
    "enable_cache_warmup": true,
    "cache_worker_threads": 4,
    "cleanup_interval_seconds": 60
}
```

| 键名 | 类型 | 描述 |
| --- | --- | --- |
| `db_type` | string | 使用的数据库类型。可选值为 `"sqlite"`, `"mysql"`, `"postgresql"`。 |
| `sqlite_path` | string | 当 `db_type` 为 `sqlite` 时，数据库文件的路径。 |
| `mysql_*` | string/int | 当 `db_type` 为 `mysql` 时的数据库连接信息。 |
| `postgresql_*` | string/int | 当 `db_type` 为 `postgresql` 时的数据库连接信息。 |
| `http_server_enabled` | bool | 是否启用 Web 管理界面。 |
| `http_server_host` | string | Web 服务器监听的 IP 地址。`0.0.0.0` 表示监听所有网络接口。 |
| `http_server_port` | int | Web 服务器监听的端口。 |
| `http_server_static_path` | string | Web UI 静态文件（HTML, CSS, JS）所在的目录。 |
| `enable_cache_warmup` | bool | 是否在插件启动时预热缓存。启用此项可以提高首次查询的性能。 |
| `cache_worker_threads` | int | 用于处理异步缓存更新的线程数量。 |
| `cleanup_interval_seconds` | int | 清理过期临时权限的时间间隔（秒）。 |

## 📖 核心概念

（此部分内容与 `USAGE.md` 基本相同，此处略作简化）

*   **权限节点**: 标识操作的字符串，如 `essentials.command.fly`。支持 `*` 通配符和 `-` 反向权限。
*   **权限组**: 权限节点的集合，用于批量授予玩家权限。
*   **继承**: 组可以继承另一个组的权限，形成层级关系。
*   **优先级**: 解决权限冲突，数字越大优先级越高。
*   **临时权限**: 玩家可以被临时加入一个组，到期后自动移除。

## ⌨️ 命令参考

主命令为 `/bedrockauthority`，别名有 `/ba` 和 `/权限组`。

### 权限组管理

| 命令 | 描述 |
| --- | --- |
| `/ba 创建权限组 <组名>` | 创建一个新的权限组。 |
| `/ba 删除权限组 <组名>` | 删除一个已存在的权限组。 |
| `/ba 设置权限组继承 <子组名> <父组名>` | 使一个组继承另一个组的权限。 |
| `/ba 移除权限组继承 <子组名> <父组名>` | 移除两个组之间的继承关系。 |

### 权限节点管理

| 命令 | 描述 |
| --- | --- |
| `/ba 注册权限节点 <节点ID> [描述] [默认值]` | 注册一个新的权限节点。 |
| `/ba 添加权限组节点 <组名> <节点ID>` | 为一个权限组添加一个权限节点。 |
| `/ba 删除权限组节点 <组名> <节点ID>` | 从一个权限组中移除一个权限节点。 |
| `/ba 列出权限组节点 <组名>` | 列出某个权限组拥有的所有权限节点。 |

### 玩家权限管理

| 命令 | 描述 |
| --- | --- |
| `/ba 加入权限组 <玩家> <组名> [时长(小时)]` | 将玩家加入权限组，可设置临时权限。 |
| `/ba 移除权限组 <玩家> <组名>` | 将玩家从权限组中移除。 |
| `/ba 列出玩家权限组 <玩家>` | 列出玩家所属的所有权限组。 |
| `/ba 列出玩家权限节点 <玩家>` | 列出玩家当前生效的所有权限节点。 |

*注意：玩家参数支持在线玩家的选择器（如 `@p`）和离线玩家的名称。*

## 🌐 Web 管理界面

如果启用了 HTTP 服务器，您可以通过浏览器访问 `http://<您的服务器IP>:<端口>` 来打开 Web 管理界面。

Web UI 提供了以下功能：
*   **仪表盘**: 展示权限系统的总体概览。
*   **权限组管理**: 创建、删除、编辑权限组，管理权限节点和继承关系。
*   **玩家管理**: 查看玩家所属的组，手动添加或移除玩家。

## 👨‍💻 API 文档

Bedrock Authority 为开发者提供了丰富的 API，方便与其他插件集成。

*   **C++ API**: 详细的 C++ 接口说明请参考 [PermissionManagerAPI.md](docs/PermissionManagerAPI.md)。
*   **脚本 API**: 如果您使用脚本插件，请参考 [ScriptAPI.md](docs/ScriptAPI.md) 来了解如何与本插件交互。

## 🛠️ 构建

如果您想从源代码构建此插件，请遵循以下步骤：

1.  确保您已安装 [XMake](https://xmake.io/) 构建工具。
2.  克隆本仓库：
    ```bash
    git clone https://github.com/yangyangzhong82/Bedrock-Authority.git
    ```
3.  进入项目目录并执行构建命令：
    ```bash
    cd Bedrock-Authority
    xmake
    ```
4.  构建产物将位于 `build` 目录下。

## 📜 许可

本插件使用 [Mozilla Public License 2.0](LICENSE) 协议开源。
