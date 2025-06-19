#pragma once

#include <ll/api/event/Cancellable.h>
#include <ll/api/event/Event.h>
#include <string>
#include <optional>

namespace BA::permission::event {

class PlayerJoinGroupBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
protected:
    std::string&       mPlayerUuid;
    std::string&       mGroupName;
    std::optional<long long>& mExpirationTime; // Unix timestamp in seconds, nullopt for permanent

public:
    constexpr explicit PlayerJoinGroupBeforeEvent(
        std::string& playerUuid,
        std::string& groupName,
        std::optional<long long>& expirationTime
    )
    : mPlayerUuid(playerUuid),
      mGroupName(groupName),
      mExpirationTime(expirationTime) {}

public:
    std::string& getPlayerUuid() const;
    std::string& getGroupName() const;
    std::optional<long long>& getExpirationTime() const;
};

class PlayerJoinGroupAfterEvent final : public ll::event::Event {
protected:
    std::string const&         mPlayerUuid;
    std::string const&       mGroupName;
    std::optional<long long> const& mExpirationTime; // Unix timestamp in seconds, nullopt for permanent

public:
    constexpr explicit PlayerJoinGroupAfterEvent(
        std::string const& playerUuid,
        std::string const& groupName,
        std::optional<long long> const& expirationTime
    )
    : mPlayerUuid(playerUuid),
      mGroupName(groupName),
      mExpirationTime(expirationTime) {}

public:
    std::string const& getPlayerUuid() const;
    std::string const& getGroupName() const;
    std::optional<long long> const& getExpirationTime() const;
};

} // namespace BA::permission::event
