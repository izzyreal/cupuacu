#pragma once

#include <memory>

#include "../State.hpp"
#include "DropdownMenu.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "Window.hpp"

namespace cupuacu::gui
{
    class DisplaySettingsWindow
    {
    public:
        explicit DisplaySettingsWindow(State *stateToUse);
        ~DisplaySettingsWindow();

        bool isOpen() const
        {
            return window && window->isOpen();
        }

        void raise() const;

        Window *getWindow() const
        {
            return window.get();
        }

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;

        OpaqueRect *background = nullptr;
        Label *vuMeterScaleLabel = nullptr;
        DropdownMenu *vuMeterScaleDropdown = nullptr;
        Label *pixelScaleLabel = nullptr;
        DropdownMenu *pixelScaleDropdown = nullptr;

        void layoutComponents() const;
        void renderOnce() const;
        void syncVuMeterScaleSelection();
        void syncPixelScaleSelection();
        void persistDisplayProperties() const;
        void applyPixelScale(uint8_t newPixelScale);
    };
} // namespace cupuacu::gui
