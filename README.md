# Bedrock Authority

**Bedrock Authority** 是面向 Minecraft: Bedrock Edition（LeviLamina）的权限管理模组。它提供权限组、继承、优先级、通配符、反向权限、默认组、临时授权以及 C++ / JS 开发者接口，支持 SQLite、MySQL 和 PostgreSQL 三种数据库。

![GitHub Repo stars](https://img.shields.io/github/stars/yangyangzhong82/Bedrock-Authority?style=social)
![GitHub license](https://img.shields.io/github/license/yangyangzhong82/Bedrock-Authority)
![GitHub issues](https://img.shields.io/github/issues/yangyangzhong82/Bedrock-Authority)

## 功能概览

- **灵活的权限规则**：支持通配符（`*`）、反向拒绝（`-`）、不区分大小写匹配，以及权限节点默认值。
- **权限组与继承**：支持多层级权限组继承，并自动拦截循环继承。
- **优先级覆盖**：同一玩家属于多个组时，高优先级组覆盖低优先级组；同一节点匹配多条规则时，更具体的规则优先。
- **默认权限组**：未加入任何组的玩家（含临时组过期后）自动使用默认组权限，无需为每个玩家写库。
- **临时授权**：支持给玩家设置有时限的权限组，到期立即失效，并由后台定时清理过期记录。
- **多数据库后端**：内置 SQLite（开箱即用），也支持 MySQL 和 PostgreSQL。
- **内存缓存与热重载**：权限查询走内存缓存，修改后同步失效；支持游戏内 `/ba reload` 热重载缓存与运行时配置。
- **双语命令与细粒度授权**：支持英文与中文两套命令写法，每条管理命令都有独立的权限节点。
- **开发者 API**：提供 C++ 接口、事件总线钩子，以及基于 LegacyRemoteCall 的脚本 API。

## 安装

### 使用 lip（推荐）

```bash
lip install github.com/yangyangzhong82/Bedrock-Authority
```

### 手动安装

1. 前往 [Releases](https://github.com/yangyangzhong82/Bedrock-Authority/releases) 下载对应版本的压缩包（服务端选 `server`，客户端选 `client`）。
2. 解压并将其中的 `Bedrock-Authority` 文件夹放到 `mods/Bedrock-Authority/`。
3. 启动一次服务器，模组会自动生成默认配置文件 `config.json`（位于 LeviLamina 为本模组分配的配置目录下，通常在 `plugins/Bedrock-Authority/config.json`）。
4. 按需修改配置后重启服务器（或使用 `/ba reload` 重载支持的运行时项）。

> 默认使用 SQLite，无需安装额外数据库软件。只有在切换到 MySQL 或 PostgreSQL 时才需要配置对应数据库服务。

## 快速开始

主命令为 `/bedrockauthority`，常用简写为 `/ba` 或 `/权限组`。管理员（OP）可直接执行：

```text
# 1. 创建 vip 组并继承默认组 default（启动时会自动创建）
/ba group create vip
/ba group inherit vip default

# 2. 给 vip 组添加权限节点（支持通配符 * 和反向禁止 -）
/ba group grant vip myplugin.fly
/ba group grant vip -myplugin.admin.*

# 3. 将玩家加入 vip 组（永久，或填小时数表示临时）
/ba user add Steve vip
/ba user add Alex vip 720

# 4. 检查玩家是否拥有某个权限
/ba check Steve myplugin.fly
```

完整规则讲解和全部 20 条中英文命令见 [使用手册 (USAGE.md)](USAGE.md)。

## 配置说明

首次启动后生成的默认 `config.json` 如下：

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
    "cleanup_interval_seconds": 60,
    "default_group": "default"
}
```

### 生效配置项

| 配置项 | 类型 | 默认值 | 支持热重载 | 说明 |
| --- | --- | --- | --- | --- |
| `db_type` | string | `"sqlite"` | 否（需重启） | 数据库类型，可选 `"sqlite"`、`"mysql"`、`"postgresql"`。 |
| `sqlite_path` | string | `"ba.db"` | 否（需重启） | `db_type` 为 `"sqlite"` 时的数据库文件路径。 |
| `mysql_host` / `mysql_port` / `mysql_user` / `mysql_password` / `mysql_db` | string / int | `127.0.0.1` / `3306` / `root` / `""` / `ba` | 否（需重启） | `db_type` 为 `"mysql"` 时的连接参数。 |
| `postgresql_host` / `postgresql_port` / `postgresql_user` / `postgresql_password` / `postgresql_db` | string / int | `127.0.0.1` / `5432` / `postgres` / `""` / `ba` | 否（需重启） | `db_type` 为 `"postgresql"` 时的连接参数。 |
| `enable_cache_warmup` | bool | `true` | 是 | 启动或重载时是否预热缓存，加快首次权限查询。 |
| `cleanup_interval_seconds` | int | `60` | 是（须 `> 0`） | 后台清理已过期临时成员记录的时间间隔（秒）。 |
| `default_group` | string | `"default"` | 是 | 默认权限组名称。非空且组不存在时启动会自动创建；留空 `""` 表示禁用默认组。 |

### 历史保留配置项

以下字段为了兼容旧版配置文件仍保留在 `config.json` 中，当前版本**不会生效**，保持默认即可：

- `http_server_enabled`、`http_server_host`、`http_server_port`、`http_server_static_path`：网页管理功能已移除。
- `cache_worker_threads`：缓存已改为同步失效机制，不再使用后台失效线程池。

## 文档导航

- **服主 / 管理员**：[使用手册 (USAGE.md)](USAGE.md) — 权限判定规则、全部中英文命令、热重载和常见配置示例。
- **C++ 开发者**：[C++ API 文档 (docs/PermissionManagerAPI.md)](docs/PermissionManagerAPI.md) — `PermissionManager` 接口、数据结构和权限变更事件。
- **脚本开发者**：[脚本 API 文档 (docs/ScriptAPI.md)](docs/ScriptAPI.md) — 通过 `LegacyRemoteCall` 调用的 JavaScript 接口。

## 从源码构建

需要 Windows x64 环境与 [XMake](https://xmake.io/)（默认使用 `clang-cl` 工具链）：

```bash
git clone https://github.com/yangyangzhong82/Bedrock-Authority.git
cd Bedrock-Authority

# 构建服务端版本（默认 target_type=server，客户端可改为 --target_type=client）
xmake f -p windows -a x64 -m release --target_type=server
xmake
```

构建完成后：
- 模组包位于 `bin/Bedrock-Authority/`。
- 给其他 C++ 模组链接使用的头文件与导入库位于 `bin/include/Bedrock-Authority/` 和 `bin/lib/Bedrock-Authority.lib`。

## 开源协议

本项目基于 [Mozilla Public License 2.0 (MPL-2.0)](LICENSE) 开源。
