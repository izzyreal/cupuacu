#pragma once

#include <SDL3/SDL.h>

namespace cupuacu
{
    struct State;
}

namespace cupuacu::gui
{
    class Window;

    void resizeComponents(cupuacu::State *state, Window *window);
    void buildComponents(cupuacu::State *state, Window *window);
} // namespace cupuacu::gui
