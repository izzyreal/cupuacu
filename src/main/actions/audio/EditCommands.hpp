#pragma once

#include "Copy.hpp"
#include "Cut.hpp"
#include "Paste.hpp"
#include "Trim.hpp"

#include "State.hpp"

#include <cstdint>
#include <memory>

namespace cupuacu::actions::audio
{
    struct SelectionTarget
    {
        int64_t start = 0;
        int64_t length = 0;
    };

    struct PasteTarget
    {
        int64_t start = 0;
        int64_t end = -1;
    };

    inline bool hasActiveSelection(const cupuacu::State *state)
    {
        return state && state->activeDocumentSession.selection.isActive();
    }

    inline SelectionTarget selectionTarget(const cupuacu::State *state)
    {
        SelectionTarget target{};
        if (!hasActiveSelection(state))
        {
            return target;
        }

        target.start = state->activeDocumentSession.selection.getStartInt();
        target.length = state->activeDocumentSession.selection.getLengthInt();
        return target;
    }

    inline PasteTarget pasteTarget(const cupuacu::State *state)
    {
        PasteTarget target{};
        if (!state)
        {
            return target;
        }

        if (state->activeDocumentSession.selection.isActive())
        {
            target.start = state->activeDocumentSession.selection.getStartInt();
            target.end = state->activeDocumentSession.selection.getEndInt();
            return target;
        }

        target.start = state->activeDocumentSession.cursor;
        target.end = -1;
        return target;
    }

    inline void performCut(cupuacu::State *state)
    {
        if (!hasActiveSelection(state))
        {
            return;
        }
        const auto target = selectionTarget(state);
        const auto undoable =
            std::make_shared<cupuacu::actions::audio::Cut>(
                state, target.start, target.length);
        state->addAndDoUndoable(undoable);
    }

    inline void performCopy(cupuacu::State *state)
    {
        if (!hasActiveSelection(state))
        {
            return;
        }
        const auto target = selectionTarget(state);
        const auto undoable =
            std::make_shared<cupuacu::actions::audio::Copy>(
                state, target.start, target.length);
        state->addAndDoUndoable(undoable);
    }

    inline void performTrim(cupuacu::State *state)
    {
        if (!hasActiveSelection(state))
        {
            return;
        }
        const auto target = selectionTarget(state);
        const auto undoable =
            std::make_shared<cupuacu::actions::audio::Trim>(
                state, target.start, target.length);
        state->addAndDoUndoable(undoable);
    }

    inline void performPaste(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }
        const auto target = pasteTarget(state);
        const auto undoable =
            std::make_shared<cupuacu::actions::audio::Paste>(
                state, target.start, target.end);
        state->addAndDoUndoable(undoable);
    }
} // namespace cupuacu::actions::audio
