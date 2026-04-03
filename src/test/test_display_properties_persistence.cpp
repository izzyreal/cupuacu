#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "persistence/DisplayPropertiesPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    class ScopedConfigCleanup
    {
    public:
        explicit ScopedConfigCleanup(std::filesystem::path root)
            : root(std::move(root))
        {
            std::error_code ec;
            std::filesystem::remove_all(this->root, ec);
        }

        ~ScopedConfigCleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

    private:
        std::filesystem::path root;
    };
} // namespace

TEST_CASE("Display properties persistence round-trip", "[persistence]")
{
    const auto testConfigRoot =
        cupuacu::test::makeUniqueTestRoot("display-properties-round-trip");
    ScopedConfigCleanup cleanup(testConfigRoot);

    cupuacu::test::StateWithTestPaths state{testConfigRoot};
    const auto propertiesPath = state.paths->displayPropertiesPath();

    const cupuacu::persistence::DisplayProperties properties{
        .pixelScale = 4, .vuMeterScale = cupuacu::gui::VuMeterScale::K14};

    REQUIRE(cupuacu::persistence::DisplayPropertiesPersistence::save(
        propertiesPath, properties));

    nlohmann::json persistedJson;
    {
        std::ifstream in(propertiesPath, std::ios::binary);
        REQUIRE(in.good());
        in >> persistedJson;
    }

    REQUIRE(persistedJson.at("version").get<int>() == 1);
    REQUIRE(persistedJson.at("pixelScale").get<int>() == 4);
    REQUIRE(persistedJson.at("vuMeterScale").get<std::string>() == "k14");

    const auto loaded =
        cupuacu::persistence::DisplayPropertiesPersistence::load(propertiesPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->pixelScale == 4);
    REQUIRE(loaded->vuMeterScale == cupuacu::gui::VuMeterScale::K14);
}

TEST_CASE("Display properties persistence rejects missing malformed and invalid values",
          "[persistence]")
{
    REQUIRE_FALSE(
        cupuacu::persistence::DisplayPropertiesPersistence::load("").has_value());
    REQUIRE_FALSE(cupuacu::persistence::DisplayPropertiesPersistence::save(
        "", {.pixelScale = 2, .vuMeterScale = cupuacu::gui::VuMeterScale::K20}));
    REQUIRE_FALSE(cupuacu::persistence::DisplayPropertiesPersistence::save(
        "display.json",
        {.pixelScale = 3, .vuMeterScale = cupuacu::gui::VuMeterScale::K20}));

    const auto testConfigRoot =
        cupuacu::test::makeUniqueTestRoot("display-properties-invalid");
    ScopedConfigCleanup cleanup(testConfigRoot);
    const auto propertiesPath = testConfigRoot / "config-home" / "display.json";
    std::filesystem::create_directories(propertiesPath.parent_path());

    {
        std::ofstream out(propertiesPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "{ invalid";
    }
    REQUIRE_FALSE(cupuacu::persistence::DisplayPropertiesPersistence::load(
                      propertiesPath)
                      .has_value());

    {
        std::ofstream out(propertiesPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"({"version":1,"pixelScale":3,"vuMeterScale":"k20"})";
    }
    REQUIRE_FALSE(cupuacu::persistence::DisplayPropertiesPersistence::load(
                      propertiesPath)
                      .has_value());

    {
        std::ofstream out(propertiesPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"({"version":1,"pixelScale":2,"vuMeterScale":"mystery"})";
    }
    REQUIRE_FALSE(cupuacu::persistence::DisplayPropertiesPersistence::load(
                      propertiesPath)
                      .has_value());
}
