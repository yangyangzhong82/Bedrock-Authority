#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/EnumName.h"
#include "ll/api/command/SoftEnum.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/Command.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/platform/UUID.h"
#include "mc/server/commands/CommandFlag.h"
#include "ll/api/service/PlayerInfo.h"
#include "permission/PermissionManager.h"
#include "command/Command.h"
#include <string>
#include <vector> 
namespace BA::Command {
    using ll::service::PlayerInfo;
    void RegisterCommands() {
        using namespace ll::command;
        auto& Registrar = CommandRegistrar::getInstance();
        auto& pm = BA::permission::PermissionManager::getInstance(); // 获取 PermissionManager 实例

        // --- 注册/更新 SoftEnum for Permission Groups ---
        const std::string groupEnumName = ll::command::enum_name_v<SoftEnum<PermissionGroupEnum>>;
        std::vector<std::string> groupNames = pm.getAllGroups(); // 获取所有权限组名称

        if (!Registrar.hasSoftEnum(groupEnumName)) {
            Registrar.tryRegisterSoftEnum(groupEnumName, groupNames);
        } else {
            Registrar.setSoftEnumValues(groupEnumName, groupNames);
        }

        // --- 注册/更新 SoftEnum for Permission Nodes ---
        const std::string nodeEnumName = ll::command::enum_name_v<SoftEnum<PermissionNodeEnum>>;
        std::vector<std::string> nodeNames = pm.getAllPermissions(); // 获取所有权限节点名称

        if (!Registrar.hasSoftEnum(nodeEnumName)) {
            Registrar.tryRegisterSoftEnum(nodeEnumName, nodeNames);
        } else {
            Registrar.setSoftEnumValues(nodeEnumName, nodeNames);
        }
        // --- SoftEnum 注册/更新结束 ---

        auto& cmd = Registrar.getOrCreateCommand(
            "bedrockauthority",                         // 命令名
            "权限",                // 命令描述
            CommandPermissionLevel::GameDirectors,      // 权限
            CommandFlagValue::NotCheat //无需作弊
        );
        cmd.alias("权限组"); // 增加别名
        

        cmd.overload<创建权限组>()
        .text("创建权限组")
        .required("权限组id")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 创建权限组 const& param, ::Command const&) {
        bool ok = pm.createGroup(param.权限组id,"A");
        if (ok) {
            output.success("已创建权限组: " + param.权限组id);
        } else {
            output.error("创建失败");
        }
        });
        cmd.overload<列出权限组节点>()
        .text("列出权限组节点")
        .required("权限组id")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 列出权限组节点 const& param, ::Command const&) {
            if (!pm.groupExists(param.权限组id)) { 
                output.error("权限组 '" + param.权限组id + "' 不存在。");
                return;
            }
            std::vector<BA::permission::CompiledPermissionRule> compiledPermissions = pm.getPermissionsOfGroup(param.权限组id);
            std::vector<std::string> permissions;
            for (const auto& rule : compiledPermissions) {
                permissions.push_back(rule.pattern);
            }
            if (permissions.empty()) {
                output.success("权限组 '" + param.权限组id + "' 没有直接或间接的权限节点。");
            } else {
                std::string permStr = "权限组 '" + param.权限组id + "' 的权限节点: ";
                for (size_t i = 0; i < permissions.size(); ++i) {
                    permStr += permissions[i];
                    if (i < permissions.size() - 1) {
                        permStr += ", ";
                    }
                }
                output.success(permStr);
            }
        });

        // ba query
        cmd.overload<添加玩家权限组>()
        .text("加入权限组")
        .required("playerName")
        .required("权限组id")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 添加玩家权限组 const& param, ::Command const&) {
            auto results = param.playerName.results(origin); // 解析目标选择器

            if (results.empty()) {
                output.error("没有选择到玩家");
                return;
            }
            for (Player* player : results) {
                if (!player) continue; // 跳过无效的玩家指针

                std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID 字符串
                bool ok = pm.addPlayerToGroup(uuidStr, param.权限组id);
                if (ok) {
                    output.success("已将玩家加入权限组: " + param.权限组id);
                } else {
                    output.error("添加失败，权限组可能不存在或玩家已在组内");
                }
            }
        });

        cmd.overload<离线添加玩家权限组>()
        .text("加入权限组")
        .required("playerName")
        .required("权限组id")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 离线添加玩家权限组 const& param, ::Command const&) {
                // 使用 PlayerInfo 获取玩家信息
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(param.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", param.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString(); // 获取 UUID

                bool ok = pm.addPlayerToGroup(uuidStr, param.权限组id);
                if (ok) {
                    output.success("已将玩家加入权限组: " + param.权限组id);
                } else {
                    output.error("添加失败，权限组可能不存在或玩家已在组内");
                
            }
        });

        cmd.overload<列出玩家权限组>()
        .text("列出玩家权限组")
        .required("playerName")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 列出玩家权限组 const& param, ::Command const&) {
            auto results = param.playerName.results(origin); // 解析目标选择器

            if (results.empty()) {
                output.error("没有选择到玩家");
                return;
            }
            for (Player* player : results) {
                if (!player) continue; // 跳过无效的玩家指针

                std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID 字符串
                std::vector<std::string> groups = pm.getPlayerGroups(uuidStr);
                if (groups.empty()) {
                    output.success(player->getRealName() + " 不属于任何权限组。");
                } else {
                    std::string groupsStr = "玩家 " + player->getRealName() + " 所属权限组: ";
                    for (size_t i = 0; i < groups.size(); ++i) {
                        groupsStr += groups[i];
                        if (i < groups.size() - 1) {
                            groupsStr += ", ";
                        }
                    }
                    output.success(groupsStr);
                }
            }
        });

        cmd.overload<离线列出玩家权限组>()
        .text("列出玩家权限组")
        .required("playerName")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 离线列出玩家权限组 const& param, ::Command const&) {
                // 使用 PlayerInfo 获取玩家信息
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(param.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", param.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString(); // 获取 UUID
                std::vector<std::string> groups = pm.getPlayerGroups(uuidStr);
                if (groups.empty()) {
                    output.success(playerInfo.name + " 不属于任何权限组。");
                } else {
                    std::string groupsStr = "玩家 " + playerInfo.name + " 所属权限组: ";
                    for (size_t i = 0; i < groups.size(); ++i) {
                        groupsStr += groups[i];
                        if (i < groups.size() - 1) {
                            groupsStr += ", ";
                        
                    }
                    output.success(groupsStr);
                }
            }
        });

        // 添加权限组节点命令
        cmd.overload<添加权限组节点>()
        .text("添加权限组节点")
        .required("权限组id")
        .required("权限节点id")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 添加权限组节点 const& param, ::Command const&) {
            // 直接尝试添加权限节点，依赖 addPermissionToGroup 内部的组存在性检查和缓存逻辑
            bool ok = pm.addPermissionToGroup(param.权限组id, param.权限节点id);
            if (ok) {
                output.success("已将权限节点 '" + param.权限节点id + "' 添加到权限组 '" + param.权限组id + "'。");
            } else {
                if (!pm.groupExists(param.权限组id)) {
                     output.error("添加失败：权限组 '" + param.权限组id + "' 不存在。");
                } else {
                     output.error("添加失败：权限节点 '" + param.权限节点id + "' 可能已分配给权限组 '" + param.权限组id + "'。");
                }
            }
        });

        // 删除权限组节点命令
        cmd.overload<删除权限组节点>()
        .text("删除权限组节点")
        .required("权限组id")
        .required("权限节点id")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 删除权限组节点 const& param, ::Command const&) {
            if (!pm.groupExists(param.权限组id)) {
                output.error("权限组 '" + param.权限组id + "' 不存在。");
                return;
            }
            // 注意：这里假设权限节点不需要预先注册，如果需要，则要添加检查 pm.permissionExists(param.权限节点id)
            bool ok = pm.removePermissionFromGroup(param.权限组id, param.权限节点id);
            if (ok) {
                output.success("已从权限组 '" + param.权限组id + "' 删除权限节点 '" + param.权限节点id + "'。");
            } else {
                output.error("删除失败，权限组或权限节点可能不存在，或权限未分配给该组。");
            }
        });


        // 列出玩家生效的权限节点命令
        cmd.overload<列出玩家权限节点>()
        .text("列出玩家权限节点")
        .required("playerName")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 列出玩家权限节点 const& param, ::Command const&) {
            auto results = param.playerName.results(origin); // 解析目标选择器

            if (results.empty()) {
                output.error("没有选择到玩家");
                return;
            }
            for (Player* player : results) {
                if (!player) continue; // 跳过无效的玩家指针

                std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID 字符串
                std::vector<BA::permission::CompiledPermissionRule> playerCompiledPermissions = pm.getAllPermissionsForPlayer(uuidStr);
                std::vector<std::string> permissions;
                for (const auto& rule : playerCompiledPermissions) {
                    if (rule.state) { // 只添加值为 true 的权限
                        permissions.push_back(rule.pattern);
                    }
                }
                if (permissions.empty()) {
                    output.success("玩家 " + player->getRealName() + " 没有生效的权限节点。");
                } else {
                    std::string permStr = "玩家 " + player->getRealName() + " 生效的权限节点: ";
                    for (size_t i = 0; i < permissions.size(); ++i) {
                        permStr += permissions[i];
                        if (i < permissions.size() - 1) {
                            permStr += ", ";
                        }
                    }
                    output.success(permStr);
                }
            }
        });
        cmd.overload<离线列出玩家权限组节点>()
        .text("列出玩家权限节点")
        .required("playerName")
        .execute(
            [](CommandOrigin const& origin, CommandOutput& output, 离线列出玩家权限组节点 const& args, ::Command const&) {

                // 使用 PlayerInfo 获取玩家信息
                auto playerInfoOpt = PlayerInfo::getInstance().fromName(args.playerName);
                if (!playerInfoOpt.has_value()) {
                    output.error(fmt::format("Player '{}' not found.", args.playerName));
                    return;
                }
                const auto& playerInfo = playerInfoOpt.value();
                std::string uuidStr = playerInfo.uuid.asString(); // 获取 UUID

                std::vector<BA::permission::CompiledPermissionRule> playerCompiledPermissions = BA::permission::PermissionManager::getInstance().getAllPermissionsForPlayer(uuidStr);
                std::vector<std::string> permissions;
                for (const auto& rule : playerCompiledPermissions) {
                    if (rule.state) { // 只添加值为 true 的权限
                        permissions.push_back(rule.pattern);
                    }
                }
                if (permissions.empty()) {
                    output.success("玩家 " + playerInfo.name + " 没有生效的权限节点。");
                } else {
                    std::string permStr = "玩家 " + playerInfo.name + " 生效的权限节点: ";
                    for (size_t i = 0; i < permissions.size(); ++i) {
                        permStr += permissions[i];
                        if (i < permissions.size() - 1) {
                            permStr += ", ";
                        }
                    }
                    output.success(permStr);
                }
            }
        );


        cmd.overload<删除权限组>()
        .text("删除权限组")
        .required("权限组id")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 删除权限组 const& param, ::Command const&) {
            bool ok = pm.deleteGroup(param.权限组id); 
            if (ok) {
                output.success("已删除权限组: " + param.权限组id);
            } else {
                output.error("删除失败，权限组可能不存在");
            }
        });



        // 新增：注册权限节点命令
        cmd.overload<注册权限节点>()
        .text("注册权限节点")
        .required("权限节点id")
        .optional("描述") // 将描述设为可选
        .optional("默认值") // 将默认值设为可选
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 注册权限节点 const& param, ::Command const&) {
            bool ok = pm.registerPermission(param.权限节点id, param.描述, param.默认值);
            if (ok) {
                output.success("已注册权限节点: " + param.权限节点id);
            } else {
                output.error("注册失败，权限节点可能已存在或发生错误。");
            }
        });

        // 移除玩家权限组命令
        cmd.overload<移除玩家权限组>()
        .text("移除权限组") // 命令的子字符串，例如 /ba 移除权限组
        .required("playerName") // 第一个必需参数：玩家选择器
        .required("权限组id") // 第二个必需参数：权限组ID
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 移除玩家权限组 const& param, ::Command const&) {
            auto results = param.playerName.results(origin); // 解析目标选择器

            if (results.empty()) {
                output.error("没有选择到玩家");
                return;
            }
            // 检查权限组是否存在
            if (!pm.groupExists(param.权限组id)) { 
                 output.error("权限组 '" + param.权限组id + "' 不存在。");
                 return;
            }

            for (Player* player : results) {
                if (!player) continue; // 跳过无效的玩家指针

                std::string uuidStr = player->getUuid().asString(); // 获取玩家 UUID 字符串
                bool ok = pm.removePlayerFromGroup(uuidStr, param.权限组id); 
                if (ok) {
                    output.success("已将玩家 " + player->getRealName() + " 从权限组 '" + param.权限组id + "' 移除。");
                } else {
                    // 提供更详细的错误信息
                    std::vector<std::string> groups = pm.getPlayerGroups(uuidStr);
                    bool isInGroup = false;
                    for(const auto& group : groups) {
                        if (group == param.权限组id) { 
                            isInGroup = true;
                            break;
                        }
                    }
                    if (!isInGroup) {
                         // 如果玩家本来就不在组里
                         output.error("移除失败，玩家 " + player->getRealName() + " 不在权限组 '" + param.权限组id + "' 中。");
                    } else {
                         // 其他未知错误
                         output.error("从权限组移除玩家 " + player->getRealName() + " 时发生未知错误。");
                    }
                }
            }
        });
        // 移除玩家权限组命令
        cmd.overload<离线移除玩家权限组>()
        .text("移除权限组") // 命令的子字符串，例如 /ba 移除权限组
        .required("playerName") // 第一个必需参数：玩家选择器
        .required("权限组id") // 第二个必需参数：权限组ID
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 离线移除玩家权限组 const& param, ::Command const&) {
            auto playerInfoOpt = PlayerInfo::getInstance().fromName(param.playerName);
            if (!playerInfoOpt.has_value()) {
                output.error(fmt::format("Player '{}' not found.", param.playerName));
                return;
            }
            const auto& playerInfo = playerInfoOpt.value();
            std::string uuidStr = playerInfo.uuid.asString(); // 获取 UUID

            // 检查权限组是否存在
            if (!pm.groupExists(param.权限组id)) {
                 output.error("权限组 '" + param.权限组id + "' 不存在。");
                 return;
            }

                bool ok = pm.removePlayerFromGroup(uuidStr, param.权限组id);
                if (ok) {
                    output.success("已将玩家 " + playerInfo.name + " 从权限组 '" + param.权限组id + "' 移除。");
                } else {
                    // 提供更详细的错误信息
                    std::vector<std::string> groups = pm.getPlayerGroups(uuidStr);
                    bool isInGroup = false;
                    for(const auto& group : groups) {
                        if (group == param.权限组id) {
                            isInGroup = true;
                            break;
                        
                    }
                    if (!isInGroup) {
                         // 如果玩家本来就不在组里
                         output.error("移除失败，玩家 " + playerInfo.name + " 不在权限组 '" + param.权限组id + "' 中。");
                    } else {
                         // 其他未知错误
                         output.error("从权限组移除玩家 " + playerInfo.name + " 时发生未知错误。");
                    }
                }
            }
        });

        // 设置权限组继承命令
        cmd.overload<设置权限组继承>()
        .text("设置权限组继承") // 命令的子字符串，例如 /ba 设置权限组继承
        .required("子权限组id") // 第一个必需参数：子权限组ID
        .required("父权限组id") // 第二个必需参数：父权限组ID
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 设置权限组继承 const& param, ::Command const&) {
            // 检查子权限组是否存在
            if (!pm.groupExists(param.子权限组id)) {
                 output.error("子权限组 '" + param.子权限组id + "' 不存在。");
                 return;
            }
            // 检查父权限组是否存在
            if (!pm.groupExists(param.父权限组id)) {
                 output.error("父权限组 '" + param.父权限组id + "' 不存在。");
                 return;
            }

            bool ok = pm.addGroupInheritance(param.子权限组id, param.父权限组id);
            if (ok) {
                output.success("已设置权限组 '" + param.子权限组id + "' 继承自 '" + param.父权限组id + "'。");
            } else {
                 // 提供更详细的错误信息，例如循环继承或已存在继承关系
                 output.error("设置继承失败。可能原因：已存在此继承关系，或形成了循环继承。");
            }
        });

        // 移除权限组继承命令
        cmd.overload<移除权限组继承>()
        .text("移除权限组继承") // 命令的子字符串
        .required("子权限组id") // 子权限组ID
        .required("父权限组id") // 父权限组ID
        .execute([&](CommandOrigin const& origin, CommandOutput& output, 移除权限组继承 const& param, ::Command const&) {
            // 检查子权限组是否存在（虽然移除时可能不需要，但保持一致性）
            if (!pm.groupExists(param.子权限组id)) {
                 output.error("子权限组 '" + param.子权限组id + "' 不存在。");
                 return;
            }
            // 检查父权限组是否存在（同上）
            if (!pm.groupExists(param.父权限组id)) {
                 output.error("父权限组 '" + param.父权限组id + "' 不存在。");
                 return;
            }

            bool ok = pm.removeGroupInheritance(param.子权限组id, param.父权限组id);
            if (ok) {
                output.success("已移除权限组 '" + param.子权限组id + "' 对 '" + param.父权限组id + "' 的继承关系。");
            } else {
                 // 提供更详细的错误信息，例如继承关系原本就不存在
                 output.error("移除继承失败。可能原因：该继承关系不存在。");
            }
        });
    }

}
