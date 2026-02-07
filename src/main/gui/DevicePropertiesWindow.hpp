#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../State.hpp"
#include "DropdownMenu.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "Window.hpp"

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
        bool syncSelectionToAudioDevices();
        void layoutComponents() const;
        void renderOnce() const;
    };
} // namespace cupuacu::gui
