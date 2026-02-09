#pragma once

#include "../Undoable.hpp"
#include "../ViewPolicy.hpp"

namespace cupuacu::actions::audio
{
    class DurationMutationUndoable : public Undoable
    {
    public:
        explicit DurationMutationUndoable(cupuacu::State *stateToUse)
            : Undoable(stateToUse)
        {
            updateGui = [this]
            {
                afterDurationMutationUi();
                cupuacu::actions::applyDurationChangeViewPolicy(this->state);
            };
        }

    protected:
        virtual void afterDurationMutationUi()
        {
        }
    };
} // namespace cupuacu::actions::audio
