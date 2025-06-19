#include "permission/events/GroupPermissionChangeEvent.h"
#include <ll/api/event/Emitter.h>

namespace BA::permission::event {

std::string& GroupPermissionChangeBeforeEvent::getGroupName() const { return mGroupName; }
std::string& GroupPermissionChangeBeforeEvent::getPermissionRule() const { return mPermissionRule; }
bool&        GroupPermissionChangeBeforeEvent::isAdd() const { return mIsAdd; }

class GroupPermissionChangeBeforeEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, GroupPermissionChangeBeforeEvent> {};


std::string const& GroupPermissionChangeAfterEvent::getGroupName() const { return mGroupName; }
std::string const& GroupPermissionChangeAfterEvent::getPermissionRule() const { return mPermissionRule; }
bool const&        GroupPermissionChangeAfterEvent::isAdd() const { return mIsAdd; }

class GroupPermissionChangeAfterEventEmitter
: public ll::event::Emitter<[](auto&&...) { return nullptr; }, GroupPermissionChangeAfterEvent> {};

} // namespace BA::permission::event
