#pragma once

#include <memory>

#include "../State.hpp"
#include "Component.hpp"
#include "DropdownMenu.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"

namespace cupuacu::gui
{
    inline std::vector<std::string> displayPixelScaleOptionLabels()
    {
        return {"1", "2", "4"};
    }

    inline int displayPixelScaleToIndex(const uint8_t pixelScale)
    {
        switch (pixelScale)
        {
        case 2:
            return 1;
        case 4:
            return 2;
        case 1:
        default:
            return 0;
        }
    }

    inline uint8_t displayPixelScaleFromIndex(const int index)
    {
        switch (index)
        {
        case 1:
            return 2;
        case 2:
            return 4;
        case 0:
        default:
            return 1;
        }
    }

    inline double adjustSamplesPerPixelForDisplayPixelScaleChange(
        const double samplesPerPixel, const uint8_t oldPixelScale,
        const uint8_t newPixelScale)
    {
        if (oldPixelScale == 0 || newPixelScale == 0)
        {
            return samplesPerPixel;
        }

        return samplesPerPixel * static_cast<double>(newPixelScale) /
               static_cast<double>(oldPixelScale);
    }

    class DisplaySettingsPane : public Component
    {
    public:
        explicit DisplaySettingsPane(State *stateToUse);
        ~DisplaySettingsPane() override = default;

        void resized() override;

    private:
        OpaqueRect *background = nullptr;
        Label *vuMeterScaleLabel = nullptr;
        DropdownMenu *vuMeterScaleDropdown = nullptr;
        Label *pixelScaleLabel = nullptr;
        DropdownMenu *pixelScaleDropdown = nullptr;

        void layoutComponents() const;
        void syncVuMeterScaleSelection();
        void syncPixelScaleSelection();
        void persistDisplayProperties() const;
        void applyPixelScale(uint8_t newPixelScale);
    };
} // namespace cupuacu::gui
