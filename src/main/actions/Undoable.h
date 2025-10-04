#pragma once

#include "../State.h"

#include <functional>

namespace cupuacu::actions {
class Undoable {
    protected:
        cupuacu::State *state;
    public:
        explicit Undoable(cupuacu::State *stateToUse) : state(stateToUse) {}

        std::function<void()> updateGui = []{};

        virtual void perform() = 0;
        virtual void undo() = 0;
};
}

