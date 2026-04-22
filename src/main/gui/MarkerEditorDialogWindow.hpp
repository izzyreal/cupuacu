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

namespace cupuacu::gui
{
    class MarkerEditorDialogWindow
    {
    public:
        explicit MarkerEditorDialogWindow(State *stateToUse, uint64_t markerIdToUse);
        ~MarkerEditorDialogWindow();

        bool isOpen() const;
        void raise() const;
        uint64_t getMarkerId() const
        {
            return markerId;
        }

    private:
        State *state = nullptr;
        uint64_t markerId = 0;
        std::unique_ptr<Window> window;
        OpaqueRect *background = nullptr;
        Label *nameLabel = nullptr;
        Label *positionLabel = nullptr;
        TextInput *nameInput = nullptr;
        TextInput *positionInput = nullptr;
        TextButton *cancelButton = nullptr;
        TextButton *deleteButton = nullptr;
        TextButton *okButton = nullptr;

        void requestClose();
        void detachFromState();
        void layoutComponents() const;
        std::optional<int64_t> parsedFrame() const;
        void applyChanges();
        void deleteMarker();
    };
} // namespace cupuacu::gui
