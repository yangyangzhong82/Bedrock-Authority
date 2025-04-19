#pragma once

#include "ll/api/mod/NativeMod.h"
#include <memory>
#include "config/Config.h"
#include "db/IDatabase.h"

namespace BA {

class MyMod {

public:
    static MyMod& getInstance();

    MyMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return True if the mod is loaded successfully.
    bool load();

    /// @return True if the mod is enabled successfully.
    bool enable();

    /// @return True if the mod is disabled successfully.
    bool disable();

    // TODO: Implement this method if you need to unload the mod.
    // /// @return True if the mod is unloaded successfully.
    // bool unload();

private:
    ll::mod::NativeMod& mSelf;
    BA::config::Config config_;
    std::unique_ptr<db::IDatabase> db_;
};

} // namespace my_mod
