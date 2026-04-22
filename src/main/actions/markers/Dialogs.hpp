#pragma once

#include "../../State.hpp"
#include "../../gui/MarkerEditorDialogWindow.hpp"

namespace cupuacu::actions::markers
{
    inline void showMarkerEditorDialog(State *state, const uint64_t markerId)
    {
        if (!state)
        {
            return;
        }

        if (!state->markerEditorDialogWindow ||
            !state->markerEditorDialogWindow->isOpen() ||
            state->markerEditorDialogWindow->getMarkerId() != markerId)
        {
            state->markerEditorDialogWindow.reset(
                new gui::MarkerEditorDialogWindow(state, markerId));
            return;
        }

        state->markerEditorDialogWindow->raise();
    }
} // namespace cupuacu::actions::markers
