#pragma once

#include "../../State.hpp"
#include "EditCommands.hpp"
#include "../../gui/MarkerEditorDialogWindow.hpp"

namespace cupuacu::actions::markers
{
    inline std::optional<uint64_t>
    resolveMarkerDialogSelection(const State *state,
                                 const std::optional<uint64_t> preferredMarkerId)
    {
        if (!state)
        {
            return std::nullopt;
        }

        const auto &document = state->getActiveDocumentSession().document;
        if (preferredMarkerId.has_value() &&
            findMarkerById(document, *preferredMarkerId).has_value())
        {
            return preferredMarkerId;
        }

        const auto selectedMarkerId = state->getActiveViewState().selectedMarkerId;
        if (selectedMarkerId.has_value() &&
            findMarkerById(document, *selectedMarkerId).has_value())
        {
            return selectedMarkerId;
        }

        const auto &markers = document.getMarkers();
        if (!markers.empty())
        {
            return markers.front().id;
        }

        return std::nullopt;
    }

    inline void showMarkerEditorDialog(
        State *state,
        const std::optional<uint64_t> preferredMarkerId = std::nullopt)
    {
        if (!state)
        {
            return;
        }

        const auto markerId = resolveMarkerDialogSelection(state, preferredMarkerId);
        if (!state->markerEditorDialogWindow ||
            !state->markerEditorDialogWindow->isOpen())
        {
            state->markerEditorDialogWindow.reset(new gui::MarkerEditorDialogWindow(
                state, markerId));
        }
        else if (preferredMarkerId.has_value())
        {
            state->markerEditorDialogWindow->selectMarker(markerId);
        }

        state->markerEditorDialogWindow->raise();
    }
} // namespace cupuacu::actions::markers
