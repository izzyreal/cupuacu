#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../State.hpp"
#include "Component.hpp"
#include "DropdownMenu.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"

namespace cupuacu::gui
{
    class DevicePropertiesPane : public Component
    {
    public:
        explicit DevicePropertiesPane(State *stateToUse);
        ~DevicePropertiesPane() override;

        void resized() override;

    private:
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
    };
} // namespace cupuacu::gui
