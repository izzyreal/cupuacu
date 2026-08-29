#pragma once

namespace cupuacu
{
    struct State;
}

namespace cupuacu::platform::macos
{
    void configureApplicationMenu(State *state);
    void clearApplicationMenuState();
} // namespace cupuacu::platform::macos
