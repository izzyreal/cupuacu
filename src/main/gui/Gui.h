#pragma once

#include <SDL3/SDL.h>

namespace cupuacu {
    struct State;
}

namespace cupuacu::gui {
void resizeComponents(cupuacu::State *state);
void buildComponents(cupuacu::State *state);
}
