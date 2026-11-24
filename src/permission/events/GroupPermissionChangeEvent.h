#pragma once

#include "../PermissionData.h"
#include <ll/api/event/Cancellable.h>
#include <ll/api/event/Event.h>
#include <string>
#include <optional>

namespace BA::permission::event {

class GroupPermissionChangeBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
protected:
    std::string& mGroupName;
    std::string& mPermissionRule;
    bool&        mIsAdd; 

public:
    constexpr explicit GroupPermissionChangeBeforeEvent(
        std::string& groupName,
        std::string& permissionRule,
        bool&        isAdd
    )
    : mGroupName(groupName),
      mPermissionRule(permissionRule),
      mIsAdd(isAdd) {}

public:
    BA_API std::string& getGroupName() const;
    BA_API std::string& getPermissionRule() const;
    BA_API bool&        isAdd() const;
};

class GroupPermissionChangeAfterEvent final : public ll::event::Event {
protected:
    std::string const& mGroupName;
    std::string const& mPermissionRule;
    bool const&        mIsAdd;

public:
    constexpr explicit GroupPermissionChangeAfterEvent(
        std::string const& groupName,
        std::string const& permissionRule,
        bool const&        isAdd
    )
    : mGroupName(groupName),
      mPermissionRule(permissionRule),
      mIsAdd(isAdd) {}

public:
    BA_API std::string const& getGroupName() const;
    BA_API std::string const& getPermissionRule() const;
    BA_API bool const&        isAdd() const;
};

} // namespace BA::permission::event
