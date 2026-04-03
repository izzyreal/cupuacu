#pragma once

#include "gui/VuMeterScale.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace cupuacu::persistence
{
    struct DisplayProperties
    {
        uint8_t pixelScale = 1;
        gui::VuMeterScale vuMeterScale = gui::VuMeterScale::PeakDbfs;
    };

    class DisplayPropertiesPersistence
    {
    public:
        static bool save(const std::filesystem::path &path,
                         const DisplayProperties &properties);

        static std::optional<DisplayProperties>
        load(const std::filesystem::path &path);
    };
} // namespace cupuacu::persistence
