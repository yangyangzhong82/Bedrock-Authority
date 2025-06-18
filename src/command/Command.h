#include "mc/server/commands/CommandSelector.h" // 包含 CommandSelector
#include "mc/world/actor/player/Player.h"       // 包含 Player
#include "ll/api/command/SoftEnum.h" // 新增: 包含 SoftEnum
#include <string>
#include <optional> // 新增: 包含 optional

namespace BA::Command {
    void RegisterCommands();
    enum class CurrencyTypeEnum {};
    // 定义权限组和权限节点的 SoftEnum 类型
    enum class PermissionGroupEnum {};
    enum class PermissionNodeEnum {};

    struct 添加玩家权限组 {
        CommandSelector<Player> playerName;         // 目标玩家选择器
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
        float 可选过期时间 = 0.0f; // 新增：可选参数，以小时为单位，改为 float 类型，并设置默认值
    };
    struct 列出玩家权限组 {
        CommandSelector<Player> playerName;         // 目标玩家选择器

    };
    struct 离线列出玩家权限组 {
        std::string playerName;         // 目标玩家选择器

    };

    struct 离线添加玩家权限组 {
        std::string playerName;         // 目标玩家选择器
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
        float 可选过期时间 = 0.0f; // 新增：可选参数，以小时为单位，改为 float 类型，并设置默认值
    };

    struct 离线列出玩家权限组节点 {
        std::string             playerName;     // 玩家名称
    };
    struct 离线移除玩家权限组 {
        std::string             playerName;     // 玩家名称
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
    };
    
    // 新增：列出玩家权限节点的结构体
    struct 列出玩家权限节点 {
        CommandSelector<Player> playerName;         // 目标玩家选择器
    };
    struct 创建权限组 {      // 目标玩家选择器
        std::string 权限组id;  
    };
    struct 列出权限组节点 {      // 目标玩家选择器
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
    };
    // 新增：添加权限组节点的结构体
    struct 添加权限组节点 {
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
        ll::command::SoftEnum<PermissionNodeEnum> 权限节点id;
    };
    // 新增：删除权限组节点的结构体
    struct 删除权限组节点 {
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
        ll::command::SoftEnum<PermissionNodeEnum> 权限节点id;
    };
    struct 删除权限组 {
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
    };

    // 新增：注册权限节点的结构体
    struct 注册权限节点 {
        std::string 权限节点id;
        std::string 描述 = ""; // 可选参数，默认为空字符串
        bool 默认值 = false; // 可选参数，默认为 false
    };

    // 新增：移除玩家权限组的结构体
    struct 移除玩家权限组 {
        CommandSelector<Player> playerName;         // 目标玩家选择器
        ll::command::SoftEnum<PermissionGroupEnum> 权限组id;
    };

    // 新增：设置权限组继承的结构体
    struct 设置权限组继承 {
        ll::command::SoftEnum<PermissionGroupEnum> 子权限组id;
        ll::command::SoftEnum<PermissionGroupEnum> 父权限组id;
    };

    // 新增：移除权限组继承的结构体
    struct 移除权限组继承 {
        ll::command::SoftEnum<PermissionGroupEnum> 子权限组id;
        ll::command::SoftEnum<PermissionGroupEnum> 父权限组id;
    };
}
