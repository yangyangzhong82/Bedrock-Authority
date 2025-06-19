#include "permission/events/PlayerJoinGroupEvent.h"
#include "permission/events/PlayerLeaveGroupEvent.h"
#include "permission/events/GroupPermissionChangeEvent.h"
#include <ll/api/event/EventBus.h>
#include <ll/api/event/Listener.h>
#include <ll/api/mod/NativeMod.h>
#include <ll/api/io/Logger.h>

namespace BA::permission::event {

// 注册事件监听器，用于测试各种权限事件
void registerTestListeners() {
    auto& logger = ::ll::mod::NativeMod::current()->getLogger();

    // 监听 PlayerJoinGroupBeforeEvent (玩家加入组前事件)
    ll::event::EventBus::getInstance().emplaceListener<PlayerJoinGroupBeforeEvent>(
        [&logger](PlayerJoinGroupBeforeEvent& event) {
            logger.debug("PlayerJoinGroupBeforeEvent 触发!");
            logger.debug("  玩家 UUID: {}", event.getPlayerUuid());
            logger.debug("  组名: {}", event.getGroupName());
            if (event.getExpirationTime().has_value()) {
                logger.debug("  过期时间: {}", event.getExpirationTime().value());
            } else {
                logger.debug("  过期时间: 永久");
            }
            if (event.isCancelled()) {
                logger.debug("  事件已被取消.");
            }
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );

    // 监听 PlayerJoinGroupAfterEvent (玩家加入组后事件)
    ll::event::EventBus::getInstance().emplaceListener<PlayerJoinGroupAfterEvent>(
        [&logger](PlayerJoinGroupAfterEvent& event) {
            logger.debug("PlayerJoinGroupAfterEvent 触发!");
            logger.debug("  玩家 UUID: {}", event.getPlayerUuid());
            logger.debug("  组名: {}", event.getGroupName());
            if (event.getExpirationTime().has_value()) {
                logger.debug("  过期时间: {}", event.getExpirationTime().value());
            } else {
                logger.debug("  过期时间: 永久");
            }
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );

    // 监听 PlayerLeaveGroupBeforeEvent (玩家离开组前事件)
    ll::event::EventBus::getInstance().emplaceListener<PlayerLeaveGroupBeforeEvent>(
        [&logger](PlayerLeaveGroupBeforeEvent& event) {
            logger.debug("PlayerLeaveGroupBeforeEvent 触发!");
            logger.debug("  玩家 UUID: {}", event.getPlayerUuid());
            logger.debug("  组名: {}", event.getGroupName());
            if (event.isCancelled()) {
                logger.debug("  事件已被取消.");
            }
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );

    // 监听 PlayerLeaveGroupAfterEvent (玩家离开组后事件)
    ll::event::EventBus::getInstance().emplaceListener<PlayerLeaveGroupAfterEvent>(
        [&logger](PlayerLeaveGroupAfterEvent& event) {
            logger.debug("PlayerLeaveGroupAfterEvent 触发!");
            logger.debug("  玩家 UUID: {}", event.getPlayerUuid());
            logger.debug("  组名: {}", event.getGroupName());
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );

    // 监听 GroupPermissionChangeBeforeEvent (组权限变更前事件)
    ll::event::EventBus::getInstance().emplaceListener<GroupPermissionChangeBeforeEvent>(
        [&logger](GroupPermissionChangeBeforeEvent& event) {
            logger.debug("GroupPermissionChangeBeforeEvent 触发!");
            logger.debug("  组名: {}", event.getGroupName());
            logger.debug("  权限规则: {}", event.getPermissionRule());
            logger.debug("  是否添加: {}", event.isAdd());
            if (event.isCancelled()) {
                logger.debug("  事件已被取消.");
            }
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );

    // 监听 GroupPermissionChangeAfterEvent (组权限变更后事件)
    ll::event::EventBus::getInstance().emplaceListener<GroupPermissionChangeAfterEvent>(
        [&logger](GroupPermissionChangeAfterEvent& event) {
            logger.debug("GroupPermissionChangeAfterEvent 触发!");
            logger.debug("  组名: {}", event.getGroupName());
            logger.debug("  权限规则: {}", event.getPermissionRule());
            logger.debug("  是否添加: {}", event.isAdd());
        },
        ll::event::EventPriority::Normal,
        ll::mod::NativeMod::current()
    );
}

// 触发测试事件
void triggerTestEvents() {
    // 玩家加入组事件测试数据
    std::string playerUuidStr = "00000000-0000-0000-0000-000000000001";
    std::string groupName = "test_group";
    std::optional<long long> expirationTime = std::nullopt; // 永久

    // 触发 PlayerJoinGroupBeforeEvent (玩家加入组前事件)
    PlayerJoinGroupBeforeEvent beforeJoinEvent(playerUuidStr, groupName, expirationTime);
    ll::event::EventBus::getInstance().publish(beforeJoinEvent);

    // 触发 PlayerJoinGroupAfterEvent (玩家加入组后事件)
    PlayerJoinGroupAfterEvent afterJoinEvent(playerUuidStr, groupName, expirationTime);
    ll::event::EventBus::getInstance().publish(afterJoinEvent);

    // 玩家离开组事件测试数据
    std::string playerUuidLeaveStr = "00000000-0000-0000-0000-000000000002";
    std::string groupNameLeave = "another_group";
    // 触发 PlayerLeaveGroupBeforeEvent (玩家离开组前事件)
    PlayerLeaveGroupBeforeEvent beforeLeaveEvent(playerUuidLeaveStr, groupNameLeave);
    ll::event::EventBus::getInstance().publish(beforeLeaveEvent);

    // 触发 PlayerLeaveGroupAfterEvent (玩家离开组后事件)
    PlayerLeaveGroupAfterEvent afterLeaveEvent(playerUuidLeaveStr, groupNameLeave);
    ll::event::EventBus::getInstance().publish(afterLeaveEvent);

    // 组权限变更事件测试数据
    std::string groupNamePerm = "admin_group";
    std::string permissionRule = "permission.test";
    bool isAdd = true; // true 表示添加权限，false 表示移除权限
    // 触发 GroupPermissionChangeBeforeEvent (组权限变更前事件)
    GroupPermissionChangeBeforeEvent beforePermChangeEvent(groupNamePerm, permissionRule, isAdd);
    ll::event::EventBus::getInstance().publish(beforePermChangeEvent);

    // 触发 GroupPermissionChangeAfterEvent (组权限变更后事件)
    GroupPermissionChangeAfterEvent afterPermChangeEvent(groupNamePerm, permissionRule, isAdd);
    ll::event::EventBus::getInstance().publish(afterPermChangeEvent);
}

} // namespace BA::permission::event
