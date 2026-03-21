#pragma once

#include "../State.hpp"

#include <string>

namespace cupuacu::gui
{
#ifdef __APPLE__
    inline std::string menuBarPrimaryShortcut(const std::string &key)
    {
        return " (Cmd + " + key + ")";
    }

    inline std::string menuBarRedoShortcut()
    {
        return " (Cmd + Shift + Z)";
    }
#else
    inline std::string menuBarPrimaryShortcut(const std::string &key)
    {
        return " (Ctrl + " + key + ")";
    }

    inline std::string menuBarRedoShortcut()
    {
        return " (Ctrl + Shift + Z)";
    }
#endif

    inline std::string buildUndoMenuLabel(cupuacu::State *state)
    {
        auto description = state->getUndoDescription();
        if (!description.empty())
        {
            description.insert(0, " ");
        }
        return "Undo" + description + menuBarPrimaryShortcut("Z");
    }

    inline std::string buildRedoMenuLabel(cupuacu::State *state)
    {
        auto description = state->getRedoDescription();
        if (!description.empty())
        {
            description.insert(0, " ");
        }
        return "Redo" + description + menuBarRedoShortcut();
    }

    inline bool isSelectionEditAvailable(const cupuacu::State *state)
    {
        return state->getActiveDocumentSession().selection.isActive();
    }

    inline bool isPasteAvailable(const cupuacu::State *state)
    {
        return state->clipboard.getFrameCount() > 0;
    }
} // namespace cupuacu::gui
