#pragma once

#include "Cut.hpp"

namespace cupuacu::actions::audio
{
    class Delete final : public SelectionRemoval
    {
    public:
        Delete(State *state, int64_t start, int64_t count)
            : SelectionRemoval(state, start, count,
                               SelectionRemovalMode::Delete)
        {
        }

        Delete(State *state, int64_t start, int64_t count,
               undo::UndoStore::SegmentHandle removedHandleToUse,
               const double oldSel1ToUse, const double oldSel2ToUse,
               const int64_t oldCursorPosToUse)
            : SelectionRemoval(state, start, count,
                               std::move(removedHandleToUse), oldSel1ToUse,
                               oldSel2ToUse, oldCursorPosToUse,
                               SelectionRemovalMode::Delete)
        {
        }
    };
} // namespace cupuacu::actions::audio
