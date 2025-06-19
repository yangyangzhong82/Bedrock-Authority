#include "permission/events/PlayerJoinGroupEvent.h"
#include <ll/api/event/Emitter.h>

namespace BA::permission::event {

std::string& PlayerJoinGroupBeforeEvent::getPlayerUuid() const { return mPlayerUuid; }
std::string& PlayerJoinGroupBeforeEvent::getGroupName() const { return mGroupName; }
std::optional<long long>& PlayerJoinGroupBeforeEvent::getExpirationTime() const { return mExpirationTime; }

class PlayerJoinGroupBeforeEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, PlayerJoinGroupBeforeEvent> {};


std::string const& PlayerJoinGroupAfterEvent::getPlayerUuid() const { return mPlayerUuid; }
std::string const& PlayerJoinGroupAfterEvent::getGroupName() const { return mGroupName; }
std::optional<long long> const& PlayerJoinGroupAfterEvent::getExpirationTime() const { return mExpirationTime; }

class PlayerJoinGroupAfterEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, PlayerJoinGroupAfterEvent> {};

} // namespace BA::permission::event
