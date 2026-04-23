#pragma once

#include "Component.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "TextButton.hpp"
#include "TextInput.hpp"
#include "Window.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    class MarkerEditorDialogWindow
    {
    public:
        explicit MarkerEditorDialogWindow(
            State *stateToUse,
            std::optional<uint64_t> initialMarkerIdToUse = std::nullopt);
        ~MarkerEditorDialogWindow();

        bool isOpen() const;
        void raise() const;

        std::optional<uint64_t> getMarkerId() const
        {
            return selectedMarkerId;
        }
        void selectMarker(std::optional<uint64_t> markerIdToSelect);

    private:
        State *state = nullptr;
        std::optional<uint64_t> selectedMarkerId;
        std::unique_ptr<Window> window;
        OpaqueRect *background = nullptr;
        OpaqueRect *sidebarBackground = nullptr;
        Component *sidebarList = nullptr;
        Label *emptySidebarLabel = nullptr;
        TextButton *addButton = nullptr;
        Label *nameLabel = nullptr;
        Label *positionLabel = nullptr;
        TextInput *nameInput = nullptr;
        TextInput *positionInput = nullptr;
        Label *emptyStateLabel = nullptr;
        TextButton *closeButton = nullptr;
        TextButton *deleteButton = nullptr;
        TextButton *applyButton = nullptr;
        mutable uint64_t lastMarkerDataVersion = 0;
        mutable std::vector<TextButton *> markerButtons;

        void requestClose();
        void detachFromState();
        void layoutComponents() const;
        std::optional<int64_t> parsedFrame() const;
        void rebuildMarkerButtons();
        void syncFromSelectedMarker();
        void syncSidebarButtons() const;
        void applyChanges();
        void deleteMarker();
        void addMarker();
    };
} // namespace cupuacu::gui
