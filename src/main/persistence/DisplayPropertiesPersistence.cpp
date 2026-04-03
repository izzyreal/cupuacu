#include "persistence/DisplayPropertiesPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr int kFormatVersion = 1;

        std::string toPersistedVuMeterScale(const gui::VuMeterScale scale)
        {
            switch (scale)
            {
            case gui::VuMeterScale::K20:
                return "k20";
            case gui::VuMeterScale::K14:
                return "k14";
            case gui::VuMeterScale::K12:
                return "k12";
            case gui::VuMeterScale::PeakDbfs:
            default:
                return "peak_dbfs";
            }
        }

        std::optional<gui::VuMeterScale> parsePersistedVuMeterScale(
            const std::string &value)
        {
            if (value == "peak_dbfs")
            {
                return gui::VuMeterScale::PeakDbfs;
            }
            if (value == "k20")
            {
                return gui::VuMeterScale::K20;
            }
            if (value == "k14")
            {
                return gui::VuMeterScale::K14;
            }
            if (value == "k12")
            {
                return gui::VuMeterScale::K12;
            }
            return std::nullopt;
        }

        bool isSupportedPixelScale(const int pixelScale)
        {
            return pixelScale == 1 || pixelScale == 2 || pixelScale == 4;
        }
    } // namespace

    bool DisplayPropertiesPersistence::save(
        const std::filesystem::path &path, const DisplayProperties &properties)
    {
        if (path.empty() || !isSupportedPixelScale(properties.pixelScale))
        {
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            return false;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.good())
        {
            return false;
        }

        const nlohmann::json json{
            {"version", kFormatVersion},
            {"pixelScale", properties.pixelScale},
            {"vuMeterScale", toPersistedVuMeterScale(properties.vuMeterScale)}};
        out << json.dump(2);
        return out.good();
    }

    std::optional<DisplayProperties>
    DisplayPropertiesPersistence::load(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return std::nullopt;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in.good())
        {
            return std::nullopt;
        }

        nlohmann::json json;
        try
        {
            in >> json;
        }
        catch (const std::exception &)
        {
            return std::nullopt;
        }

        if (!json.is_object() || !json.contains("version") ||
            !json.contains("pixelScale") || !json.contains("vuMeterScale"))
        {
            return std::nullopt;
        }

        const int version = json.at("version").get<int>();
        if (version != kFormatVersion)
        {
            return std::nullopt;
        }

        const int pixelScale = json.at("pixelScale").get<int>();
        if (!isSupportedPixelScale(pixelScale))
        {
            return std::nullopt;
        }

        const auto vuMeterScale = parsePersistedVuMeterScale(
            json.at("vuMeterScale").get<std::string>());
        if (!vuMeterScale.has_value())
        {
            return std::nullopt;
        }

        return DisplayProperties{.pixelScale = static_cast<uint8_t>(pixelScale),
                                 .vuMeterScale = *vuMeterScale};
    }
} // namespace cupuacu::persistence
