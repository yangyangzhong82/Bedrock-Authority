#include "permission/events/GroupManagementEvents.h"
#include <ll/api/event/Emitter.h>

namespace BA::permission::event {
class GroupManagementEmitter : public ll::event::Emitter<
    [](auto&&...) { return nullptr; },
    GroupCreateBeforeEvent, GroupCreateAfterEvent,
    GroupDeleteBeforeEvent, GroupDeleteAfterEvent,
    GroupDescriptionChangeBeforeEvent, GroupDescriptionChangeAfterEvent,
    GroupPriorityChangeBeforeEvent, GroupPriorityChangeAfterEvent,
    GroupInheritanceChangeBeforeEvent, GroupInheritanceChangeAfterEvent
> {};
} // namespace BA::permission::event
