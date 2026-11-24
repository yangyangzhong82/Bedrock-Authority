#pragma once

#include <ll/api/event/Cancellable.h>
#include <ll/api/event/Event.h>
#include <string>

namespace BA::permission::event {

class GroupCreateBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
public:
    explicit GroupCreateBeforeEvent(std::string& groupName, std::string& description)
    : groupName(groupName), description(description) {}

    std::string& groupName;
    std::string& description;
};

class GroupCreateAfterEvent final : public ll::event::Event {
public:
    explicit GroupCreateAfterEvent(std::string const& groupName, std::string const& description)
    : groupName(groupName), description(description) {}

    std::string const& groupName;
    std::string const& description;
};

class GroupDeleteBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
public:
    explicit GroupDeleteBeforeEvent(std::string& groupName)
    : groupName(groupName) {}

    std::string& groupName;
};

class GroupDeleteAfterEvent final : public ll::event::Event {
public:
    explicit GroupDeleteAfterEvent(std::string const& groupName)
    : groupName(groupName) {}

    std::string const& groupName;
};

class GroupInheritanceChangeBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
public:
    explicit GroupInheritanceChangeBeforeEvent(std::string& groupName, std::string& parentGroupName, bool& adding)
    : groupName(groupName), parentGroupName(parentGroupName), adding(adding) {}

    std::string& groupName;
    std::string& parentGroupName;
    bool& adding;
};

class GroupInheritanceChangeAfterEvent final : public ll::event::Event {
public:
    explicit GroupInheritanceChangeAfterEvent(std::string const& groupName, std::string const& parentGroupName, bool const& adding)
    : groupName(groupName), parentGroupName(parentGroupName), adding(adding) {}

    std::string const& groupName;
    std::string const& parentGroupName;
    bool const& adding;
};

class GroupPriorityChangeBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
public:
    explicit GroupPriorityChangeBeforeEvent(std::string& groupName, int& priority)
    : groupName(groupName), priority(priority) {}

    std::string& groupName;
    int& priority;
};

class GroupPriorityChangeAfterEvent final : public ll::event::Event {
public:
    explicit GroupPriorityChangeAfterEvent(std::string const& groupName, int const& priority)
    : groupName(groupName), priority(priority) {}

    std::string const& groupName;
    int const& priority;
};

class GroupDescriptionChangeBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
public:
    explicit GroupDescriptionChangeBeforeEvent(std::string& groupName, std::string& description)
    : groupName(groupName), description(description) {}

    std::string& groupName;
    std::string& description;
};

class GroupDescriptionChangeAfterEvent final : public ll::event::Event {
public:
    explicit GroupDescriptionChangeAfterEvent(std::string const& groupName, std::string const& description)
    : groupName(groupName), description(description) {}

    std::string const& groupName;
    std::string const& description;
};

} // namespace BA::permission::event
