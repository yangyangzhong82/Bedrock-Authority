#pragma once

#include "../PermissionData.h"
#include <ll/api/event/Cancellable.h>
#include <ll/api/event/Event.h>
#include <string>
#include <optional>

namespace BA::permission::event {

class PlayerLeaveGroupBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
protected:
    std::string& mPlayerUuid;
    std::string& mGroupName;

public:
    constexpr explicit PlayerLeaveGroupBeforeEvent(
        std::string& playerUuid,
        std::string& groupName
    )
    : mPlayerUuid(playerUuid),
      mGroupName(groupName) {}

public:
    BA_API std::string& getPlayerUuid() const;
    BA_API std::string& getGroupName() const;
};

class PlayerLeaveGroupAfterEvent final : public ll::event::Event {
protected:
    std::string const& mPlayerUuid;
    std::string const& mGroupName;

public:
    constexpr explicit PlayerLeaveGroupAfterEvent(
        std::string const& playerUuid,
        std::string const& groupName
    )
    : mPlayerUuid(playerUuid),
      mGroupName(groupName) {}

public:
    BA_API std::string const& getPlayerUuid() const;
    BA_API std::string const& getGroupName() const;
};

} // namespace BA::permission::event
