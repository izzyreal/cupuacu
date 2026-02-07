#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../State.h"
#include "DropdownMenu.h"
#include "Label.h"
#include "OpaqueRect.h"
#include "Window.h"

namespace cupuacu::gui
{
    class DevicePropertiesWindow
    {
    public:
        DevicePropertiesWindow(State *stateToUse);
        ~DevicePropertiesWindow();
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
        bool ownsPortAudio = false;

        OpaqueRect *background = nullptr;
        Label *deviceTypeLabel = nullptr;
        DropdownMenu *deviceTypeDropdown = nullptr;
        Label *outputDeviceLabel = nullptr;
        DropdownMenu *outputDeviceDropdown = nullptr;
        Label *inputDeviceLabel = nullptr;
        DropdownMenu *inputDeviceDropdown = nullptr;

        std::vector<int> hostApiIndices;
        std::vector<int> outputDeviceIndices;
        std::vector<int> inputDeviceIndices;

        void populateHostApis();
        void populateDevices(const int hostApiIndex,
                             const int preferredOutputDeviceIndex,
                             const int preferredInputDeviceIndex);
        int getSelectedHostApiIndex() const;
        int getSelectedDeviceIndex(const DropdownMenu *dropdown,
                                   const std::vector<int> &indices) const;
        void syncSelectionToAudioDevices();
        void layoutComponents() const;
        void renderOnce() const;
    };
} // namespace cupuacu::gui
