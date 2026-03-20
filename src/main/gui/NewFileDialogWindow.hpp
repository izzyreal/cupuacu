#pragma once

#include "../SampleFormat.hpp"

#include "Component.hpp"
#include "DropdownMenu.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "TextButton.hpp"
#include "Window.hpp"

#include <memory>

namespace cupuacu::gui
{
    class NewFileDialogWindow
    {
    public:
        explicit NewFileDialogWindow(State *stateToUse);
        ~NewFileDialogWindow();

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
        Label *sampleRateLabel = nullptr;
        Label *bitDepthLabel = nullptr;
        DropdownMenu *sampleRateDropdown = nullptr;
        DropdownMenu *bitDepthDropdown = nullptr;
        TextButton *cancelButton = nullptr;
        TextButton *okButton = nullptr;

        void requestClose();
        void detachFromState();
        void layoutComponents() const;
        int selectedSampleRate() const;
        cupuacu::SampleFormat selectedFormat() const;
    };
} // namespace cupuacu::gui
