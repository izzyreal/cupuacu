#include <catch2/catch_test_macros.hpp>

#include "Paths.hpp"
#include "State.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"
#include "persistence/AudioDevicePropertiesPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

namespace
{
    class TestPaths : public cupuacu::Paths
    {
    protected:
        std::filesystem::path appConfigHome() const override
        {
            return std::filesystem::temp_directory_path() /
                   "cupuacu-test-config";
        }
    };

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

    class ScopedResolverOverride
    {
    public:
        explicit ScopedResolverOverride(
            cupuacu::persistence::AudioDevicePropertiesPersistence::Resolver
                resolver)
        {
            cupuacu::persistence::AudioDevicePropertiesPersistence::
                setResolverForTesting(std::move(resolver));
        }

        ~ScopedResolverOverride()
        {
            cupuacu::persistence::AudioDevicePropertiesPersistence::
                resetResolverForTesting();
        }
    };
} // namespace

TEST_CASE("Audio device properties persistence round-trip", "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config";
    ScopedConfigCleanup cleanup(testConfigRoot);

    cupuacu::State state{};
    state.paths = std::make_unique<TestPaths>();
    const auto propertiesPath = state.paths->audioDevicePropertiesPath();

    ScopedResolverOverride resolverOverride(
        cupuacu::persistence::AudioDevicePropertiesPersistence::Resolver{
            .resolveHostApiName =
                [](const int hostApiIndex)
            {
                return hostApiIndex == 7 ? std::string{"Mock Host API"}
                                         : std::string{};
            },
            .resolveDeviceName =
                [](const int deviceIndex)
            {
                if (deviceIndex == 9)
                {
                    return std::string{"Mock Output Device"};
                }
                if (deviceIndex == 11)
                {
                    return std::string{"Mock Input Device"};
                }
                return std::string{};
            },
            .resolveHostApiIndex =
                [](const std::string &hostApiName)
            {
                return hostApiName == "Mock Host API" ? 7 : -1;
            },
            .resolveDeviceIndex =
                [](const std::string &deviceName, const bool isInput,
                   const int hostApiIndex)
            {
                if (hostApiIndex != 7)
                {
                    return -1;
                }
                if (!isInput && deviceName == "Mock Output Device")
                {
                    return 9;
                }
                if (isInput && deviceName == "Mock Input Device")
                {
                    return 11;
                }
                return -1;
            }});

    cupuacu::audio::AudioDevices::DeviceSelection selection;
    selection.hostApiIndex = 7;
    selection.outputDeviceIndex = 9;
    selection.inputDeviceIndex = 11;

    REQUIRE(cupuacu::persistence::AudioDevicePropertiesPersistence::save(
        propertiesPath, selection));

    nlohmann::json persistedJson;
    {
        std::ifstream in(propertiesPath, std::ios::binary);
        REQUIRE(in.good());
        in >> persistedJson;
    }
    REQUIRE(persistedJson.at("version").get<int>() == 1);
    REQUIRE(persistedJson.contains("hostApiName"));
    REQUIRE(persistedJson.contains("outputDeviceName"));
    REQUIRE(persistedJson.contains("inputDeviceName"));
    REQUIRE(persistedJson.at("hostApiName").get<std::string>() ==
            "Mock Host API");
    REQUIRE(persistedJson.at("outputDeviceName").get<std::string>() ==
            "Mock Output Device");
    REQUIRE(persistedJson.at("inputDeviceName").get<std::string>() ==
            "Mock Input Device");

    const auto loaded =
        cupuacu::persistence::AudioDevicePropertiesPersistence::load(
            propertiesPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->hostApiIndex == 7);
    REQUIRE(loaded->outputDeviceIndex == 9);
    REQUIRE(loaded->inputDeviceIndex == 11);
}
