#pragma once

#include "Component.hpp"
#include "DropdownMenu.hpp"
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
    class GenerateSilenceDialogWindow
    {
    public:
        explicit GenerateSilenceDialogWindow(State *stateToUse);
        ~GenerateSilenceDialogWindow();

        bool isOpen() const;
        void raise() const;
        Window *getWindow() const
        {
            return window.get();
        }

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;
        OpaqueRect *background = nullptr;
        Label *durationLabel = nullptr;
        Label *unitLabel = nullptr;
        TextInput *durationInput = nullptr;
        DropdownMenu *unitDropdown = nullptr;
        TextButton *cancelButton = nullptr;
        TextButton *okButton = nullptr;

        void requestClose();
        void detachFromState();
        void layoutComponents() const;
        std::optional<int64_t> durationInFrames() const;
    };
} // namespace cupuacu::gui
