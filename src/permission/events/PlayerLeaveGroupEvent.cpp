#include "permission/events/PlayerLeaveGroupEvent.h"
#include <ll/api/event/Emitter.h>

namespace BA::permission::event {

std::string& PlayerLeaveGroupBeforeEvent::getPlayerUuid() const { return mPlayerUuid; }
std::string& PlayerLeaveGroupBeforeEvent::getGroupName() const { return mGroupName; }

class PlayerLeaveGroupBeforeEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, PlayerLeaveGroupBeforeEvent> {};


std::string const& PlayerLeaveGroupAfterEvent::getPlayerUuid() const { return mPlayerUuid; }
std::string const& PlayerLeaveGroupAfterEvent::getGroupName() const { return mGroupName; }

class PlayerLeaveGroupAfterEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, PlayerLeaveGroupAfterEvent> {};

} // namespace BA::permission::event
