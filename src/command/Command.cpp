#include "command/Command.h"
#include "mod/MyMod.h"
#include "permission/PermissionManager.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/EnumName.h"
#include "ll/api/command/SoftEnum.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/server/commands/CommandFlag.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/player/Player.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fmt/ranges.h>
#include <limits>

namespace BA::Command {
enum class PermissionGroupEnum {};
enum class PermissionNodeEnum {};
using Group = ll::command::SoftEnum<PermissionGroupEnum>;
using Node = ll::command::SoftEnum<PermissionNodeEnum>;
using Online = CommandSelector<Player>;

struct NoArgs { static void bind(auto&) {} };
struct GroupName {
    std::string group;
    static void bind(auto& o) { (void)o.required("group"); }
};
struct GroupArg {
    Group group;
    static void bind(auto& o) { (void)o.required("group"); }
};
struct Rule {
    Group group;
    Node node;
    static void bind(auto& o) { (void)o.required("group").required("node"); }
};
struct Inheritance {
    Group child;
    Group parent;
    static void bind(auto& o) { (void)o.required("child").required("parent"); }
};
struct Priority {
    Group group;
    int priority;
    static void bind(auto& o) { (void)o.required("group").required("priority"); }
};
struct Description {
    Group group;
    std::string description;
    static void bind(auto& o) { (void)o.required("group").required("description"); }
};
struct RegisterNode {
    std::string node;
    std::string description;
    bool defaultValue = false;
    static void bind(auto& o) { (void)o.required("node").optional("description").optional("defaultValue"); }
};
template <class T>
struct Target {
    T player;
    static void bind(auto& o) { (void)o.required("player"); }
};
using OnlineTarget = Target<Online>;
using NamedTarget = Target<std::string>;
template <class T>
struct Membership {
    T player;
    Group group;
    static void bind(auto& o) { (void)o.required("player").required("group"); }
};
using OnlineMembership = Membership<Online>;
using NamedMembership = Membership<std::string>;
template <class T>
struct Join {
    T player;
    Group group;
    float hours = 0;
    static void bind(auto& o) { (void)o.required("player").required("group").optional("hours"); }
};
using OnlineJoin = Join<Online>;
using NamedJoin = Join<std::string>;
template <class T>
struct Check {
    T player;
    Node node;
    static void bind(auto& o) { (void)o.required("player").required("node"); }
};
using OnlineCheck = Check<Online>;
using NamedCheck = Check<std::string>;
} // namespace BA::Command

// clang-cl may leave LL's inline variable-template definitions for command
// parameter offsets as unresolved external references.  Emit concrete,
// non-inline definitions for the parameter types used by this translation
// unit while keeping LL's layout calculation as the source of the value.
#define BA_DEFINE_MEMBER_OFFSET(index, type)                                                                           \
    template <>                                                                                                        \
    std::ptrdiff_t const ll::reflection::member_offset_v<index, BA::Command::type> =                                   \
        ll::reflection::OffsetGetter<BA::Command::type>::template offset<index>(                                       \
            std::make_index_sequence<ll::reflection::member_count_v<BA::Command::type>>{}                              \
        )

BA_DEFINE_MEMBER_OFFSET(0, GroupName);
BA_DEFINE_MEMBER_OFFSET(0, GroupArg);
BA_DEFINE_MEMBER_OFFSET(0, Rule);
BA_DEFINE_MEMBER_OFFSET(1, Rule);
BA_DEFINE_MEMBER_OFFSET(0, Inheritance);
BA_DEFINE_MEMBER_OFFSET(1, Inheritance);
BA_DEFINE_MEMBER_OFFSET(0, Priority);
BA_DEFINE_MEMBER_OFFSET(1, Priority);
BA_DEFINE_MEMBER_OFFSET(0, Description);
BA_DEFINE_MEMBER_OFFSET(1, Description);
BA_DEFINE_MEMBER_OFFSET(0, RegisterNode);
BA_DEFINE_MEMBER_OFFSET(1, RegisterNode);
BA_DEFINE_MEMBER_OFFSET(2, RegisterNode);
BA_DEFINE_MEMBER_OFFSET(0, OnlineTarget);
BA_DEFINE_MEMBER_OFFSET(0, NamedTarget);
BA_DEFINE_MEMBER_OFFSET(0, OnlineMembership);
BA_DEFINE_MEMBER_OFFSET(1, OnlineMembership);
BA_DEFINE_MEMBER_OFFSET(0, NamedMembership);
BA_DEFINE_MEMBER_OFFSET(1, NamedMembership);
BA_DEFINE_MEMBER_OFFSET(0, OnlineJoin);
BA_DEFINE_MEMBER_OFFSET(1, OnlineJoin);
BA_DEFINE_MEMBER_OFFSET(2, OnlineJoin);
BA_DEFINE_MEMBER_OFFSET(0, NamedJoin);
BA_DEFINE_MEMBER_OFFSET(1, NamedJoin);
BA_DEFINE_MEMBER_OFFSET(2, NamedJoin);
BA_DEFINE_MEMBER_OFFSET(0, OnlineCheck);
BA_DEFINE_MEMBER_OFFSET(1, OnlineCheck);
BA_DEFINE_MEMBER_OFFSET(0, NamedCheck);
BA_DEFINE_MEMBER_OFFSET(1, NamedCheck);
#undef BA_DEFINE_MEMBER_OFFSET

namespace BA::Command {
namespace {
using permission::PermissionManager;
using ll::command::CommandRegistrar;

template <class Enum>
void refreshEnum(const std::vector<std::string>& values) {
    auto& registrar = CommandRegistrar::getInstance(false);
    const std::string name = ll::command::enum_name_v<Enum>;
    if (registrar.hasSoftEnum(name)) registrar.setSoftEnumValues(name, values);
    else registrar.tryRegisterSoftEnum(name, values);
}

bool authorized(const CommandOrigin& origin, const std::string& node) {
    if (origin.getPermissionsLevel() >= CommandPermissionLevel::GameDirectors) return true;
    auto* actor = origin.getEntity();
    if (origin.getOriginType() != CommandOriginType::Player || !actor || !actor->isPlayer()) return false;
    return PermissionManager::getInstance().hasPermission(static_cast<Player*>(actor)->getUuid().asString(), node);
}

// The English words also form the suffix of the permission node, e.g. group.create.
template <class Params, class Fn>
void addCommand(ll::command::CommandHandle& cmd, std::string_view path, std::string_view chinese, Fn handler) {
    std::string node = "bedrockauthority.admin." + std::string(path);
    auto& pm = PermissionManager::getInstance();
    if (!pm.permissionExists(node)) pm.registerPermission(node, std::string(chinese), false);
    auto execute = [node, handler](const CommandOrigin& origin, CommandOutput& out, const Params& args) {
        if (!authorized(origin, node)) {
            out.error("缺少权限 " + node);
            return;
        }
        handler(origin, out, args);
    };
    auto english = cmd.overload<Params>();
    while (!path.empty()) {
        auto pos = path.find('.');
        (void)english.text(path.substr(0, pos));
        if (pos == path.npos) break;
        path.remove_prefix(pos + 1);
    }
    Params::bind(english);
    english.execute(execute);
    auto alias = cmd.overload<Params>();
    (void)alias.text(chinese);
    Params::bind(alias);
    alias.execute(execute);
}

template <class Fn>
void forPlayers(const Online& target, const CommandOrigin& origin, CommandOutput& out, Fn visit) {
    auto players = target.results(origin);
    if (players.empty()) out.error("没有选择到玩家。");
    for (auto* player : players) visit(player->getUuid().asString(), player->getRealName());
}

template <class Fn>
void forPlayers(const std::string& name, const CommandOrigin&, CommandOutput& out, Fn visit) {
    auto player = ll::service::PlayerInfo::getInstance().fromName(name);
    if (player) visit(player->uuid.asString(), player->name);
    else out.error("找不到玩家 " + name);
}

template <template <class> class Params, class Fn>
void addPlayerCommand(ll::command::CommandHandle& cmd, std::string_view path, std::string_view chinese, Fn fn) {
    auto run = [fn](const CommandOrigin& origin, CommandOutput& out, const auto& args) {
        forPlayers(args.player, origin, out, [&](const std::string& uuid, const std::string& name) {
            fn(out, args, uuid, name);
        });
    };
    addCommand<Params<Online>>(cmd, path, chinese, run);
    addCommand<Params<std::string>>(cmd, path, chinese, run);
}

void report(CommandOutput& out, bool success, const std::string& message) {
    if (success) out.success(message);
    else out.error("操作未完成，请检查目标、现有设置或事件是否被取消。");
}
} // namespace

void RegisterCommands() {
    auto& pm = PermissionManager::getInstance();
    auto& cmd = CommandRegistrar::getInstance(false).getOrCreateCommand(
        "bedrockauthority", "权限管理", CommandPermissionLevel::Any, CommandFlagValue::NotCheat
    );
    cmd.alias("权限组");
    cmd.alias("ba");

    addCommand<GroupName>(cmd, "group.create", "创建权限组", [&](auto&, auto& out, const auto& a) {
        bool ok = pm.createGroup(a.group);
        if (ok) refreshEnum<Group>(pm.getAllGroups());
        report(out, ok, "权限组已创建。");
    });
    addCommand<GroupArg>(cmd, "group.delete", "删除权限组", [&](auto&, auto& out, const auto& a) {
        bool ok = pm.deleteGroup(a.group);
        if (ok) refreshEnum<Group>(pm.getAllGroups());
        report(out, ok, "权限组已删除。");
    });
    addCommand<NoArgs>(cmd, "group.list", "列出所有权限组", [&](auto&, auto& out, const auto&) {
        auto groups = pm.getAllGroups();
        out.success(groups.empty() ? "没有权限组。" : fmt::format("权限组：{}", fmt::join(groups, ", ")));
    });
    addCommand<Priority>(cmd, "group.priority", "设置组优先级", [&](auto&, auto& out, const auto& a) {
        report(out, pm.setGroupPriority(a.group, a.priority), "权限组优先级已更新。");
    });
    addCommand<Description>(cmd, "group.description", "修改组描述", [&](auto&, auto& out, const auto& a) {
        report(out, pm.updateGroupDescription(a.group, a.description), "权限组描述已更新。");
    });
    addCommand<GroupArg>(cmd, "group.nodes", "列出权限组节点", [&](auto&, auto& out, const auto& a) {
        if (!pm.groupExists(a.group)) {
            out.error("权限组不存在。");
            return;
        }
        std::vector<std::string> rules;
        for (const auto& rule : pm.getPermissionsOfGroup(a.group)) {
            rules.push_back(rule.state ? rule.pattern : "-" + rule.pattern);
        }
        out.success(rules.empty() ? "该组没有权限节点。" : fmt::format("权限节点：{}", fmt::join(rules, ", ")));
    });
    addCommand<Rule>(cmd, "group.grant", "添加权限组节点", [&](auto&, auto& out, const auto& a) {
        report(out, pm.addPermissionToGroup(a.group, a.node), "权限组规则已更新。");
    });
    addCommand<Rule>(cmd, "group.revoke", "删除权限组节点", [&](auto&, auto& out, const auto& a) {
        report(out, pm.removePermissionFromGroup(a.group, a.node), "权限组规则已更新。");
    });
    addCommand<Inheritance>(cmd, "group.inherit", "设置权限组继承", [&](auto&, auto& out, const auto& a) {
        report(out, pm.addGroupInheritance(a.child, a.parent), "继承关系已更新。");
    });
    addCommand<Inheritance>(cmd, "group.uninherit", "移除权限组继承", [&](auto&, auto& out, const auto& a) {
        report(out, pm.removeGroupInheritance(a.child, a.parent), "继承关系已更新。");
    });
    addCommand<RegisterNode>(cmd, "node.register", "注册权限节点", [&](auto&, auto& out, const auto& a) {
        bool ok = pm.registerPermission(a.node, a.description, a.defaultValue);
        if (ok) refreshEnum<Node>(pm.getAllPermissions());
        report(out, ok, "已注册权限节点 " + a.node);
    });

    addPlayerCommand<Join>(cmd, "user.add", "加入权限组", [&](auto& out, const auto& a, auto& uuid, auto& name) {
        double seconds = static_cast<double>(a.hours) * 3600;
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (!std::isfinite(seconds) || seconds < 0 || (seconds > 0 && seconds < 1)
            || seconds >= static_cast<double>(std::numeric_limits<long long>::max() - now)) {
            out.error("时长以小时为单位，临时成员至少需要一秒，0 表示永久。");
            return;
        }
        report(out, pm.addPlayerToGroup(uuid, a.group, static_cast<long long>(seconds)), name + " 的入组请求已处理。");
    });
    addPlayerCommand<Membership>(cmd, "user.remove", "移除权限组", [&](auto& out, const auto& a, auto& uuid, auto& name) {
        report(out, pm.removePlayerFromGroup(uuid, a.group), name + " 的移组请求已处理。");
    });
    addPlayerCommand<Target>(cmd, "user.groups", "列出玩家权限组", [&](auto& out, const auto&, auto& uuid, auto& name) {
        auto groups = pm.getPlayerGroups(uuid);
        out.success(groups.empty() ? name + " 不属于任何权限组。"
                                   : fmt::format("{} 所属权限组：{}", name, fmt::join(groups, ", ")));
    });
    addPlayerCommand<Target>(cmd, "user.nodes", "列出玩家权限节点", [&](auto& out, const auto&, auto& uuid, auto& name) {
        std::vector<std::string> nodes;
        for (const auto& rule : pm.getAllPermissionsForPlayer(uuid)) {
            if (rule.state) nodes.push_back(rule.pattern);
        }
        out.success(nodes.empty() ? name + " 没有生效的权限节点。"
                                  : fmt::format("{} 生效的权限节点：{}", name, fmt::join(nodes, ", ")));
    });
    addPlayerCommand<Membership>(cmd, "user.expiry", "查看玩家组剩余时间", [&](auto& out, const auto& a, auto& uuid, auto& name) {
        for (const auto& group : pm.getPlayerGroupsWithPriorities(uuid)) {
            if (group.name != a.group) continue;
            auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();
            out.success(group.expirationTime
                ? fmt::format("{} 在 {} 组剩余 {} 秒。", name, group.name, std::max(0LL, *group.expirationTime - now))
                : fmt::format("{} 是 {} 组的永久成员。", name, group.name));
            return;
        }
        out.error(name + " 当前不属于该组。");
    });
    addPlayerCommand<Check>(cmd, "check", "检查玩家权限", [&](auto& out, const auto& a, auto& uuid, auto& name) {
        out.success(fmt::format("{} {} 权限 {}", name, pm.hasPermission(uuid, a.node) ? "拥有" : "没有", std::string(a.node)));
    });
    addCommand<NoArgs>(cmd, "reload", "重载", [&](auto&, auto& out, const auto&) {
        if (!MyMod::getInstance().reload()) {
            out.error("重载失败，请检查配置文件及服务端日志。");
            return;
        }
        refreshEnum<Group>(pm.getAllGroups());
        refreshEnum<Node>(pm.getAllPermissions());
        out.success("已重载权限缓存和运行时配置，数据库连接设置在重启后生效。");
    });

    refreshEnum<Group>(pm.getAllGroups());
    refreshEnum<Node>(pm.getAllPermissions());
}
} // namespace BA::Command
