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
#include <portaudio.h>
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

TEST_CASE("Audio device properties persistence rejects empty and missing paths",
          "[persistence]")
{
    cupuacu::audio::AudioDevices::DeviceSelection selection;
    selection.hostApiIndex = 1;
    selection.outputDeviceIndex = 2;
    selection.inputDeviceIndex = 3;

    REQUIRE_FALSE(
        cupuacu::persistence::AudioDevicePropertiesPersistence::save("", selection));
    REQUIRE_FALSE(
        cupuacu::persistence::AudioDevicePropertiesPersistence::load("").has_value());

    const auto missingPath = std::filesystem::temp_directory_path() /
                             "cupuacu-missing-audio-device-properties.json";
    std::error_code ec;
    std::filesystem::remove(missingPath, ec);
    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                      missingPath)
                      .has_value());
}

TEST_CASE("Audio device properties persistence rejects malformed or incomplete JSON",
          "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config-invalid";
    ScopedConfigCleanup cleanup(testConfigRoot);
    const auto malformedPath = testConfigRoot / "malformed.json";

    std::filesystem::create_directories(testConfigRoot);

    {
        std::ofstream out(malformedPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "{ this is not valid json";
    }
    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                      malformedPath)
                      .has_value());

    {
        std::ofstream out(malformedPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"({"version":1,"hostApiName":"Mock Host API"})";
    }
    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                      malformedPath)
                      .has_value());
}

TEST_CASE("Audio device properties persistence rejects unsupported versions",
          "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config-version";
    ScopedConfigCleanup cleanup(testConfigRoot);
    const auto versionedPath = testConfigRoot / "unsupported-version.json";

    std::filesystem::create_directories(testConfigRoot);
    {
        std::ofstream out(versionedPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"({"version":99,"hostApiName":"Mock Host API","outputDeviceName":"Mock Output Device","inputDeviceName":"Mock Input Device"})";
    }

    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                      versionedPath)
                      .has_value());
}

TEST_CASE("Audio device properties persistence maps unresolved names to -1 indexes",
          "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config-unresolved";
    ScopedConfigCleanup cleanup(testConfigRoot);
    const auto propertiesPath = testConfigRoot / "audio-device-properties.json";

    std::filesystem::create_directories(testConfigRoot);

    ScopedResolverOverride resolverOverride(
        cupuacu::persistence::AudioDevicePropertiesPersistence::Resolver{
            .resolveHostApiName = [](const int) { return std::string{}; },
            .resolveDeviceName = [](const int) { return std::string{}; },
            .resolveHostApiIndex =
                [](const std::string &) { return -1; },
            .resolveDeviceIndex =
                [](const std::string &, const bool, const int) { return -1; }});

    {
        std::ofstream out(propertiesPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"({"version":1,"hostApiName":"Unknown Host","outputDeviceName":"Unknown Output","inputDeviceName":"Unknown Input"})";
    }

    const auto loaded =
        cupuacu::persistence::AudioDevicePropertiesPersistence::load(
            propertiesPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->hostApiIndex == -1);
    REQUIRE(loaded->outputDeviceIndex == -1);
    REQUIRE(loaded->inputDeviceIndex == -1);
}

TEST_CASE("Audio device properties persistence save fails when parent path cannot be created",
          "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config-blocked-parent";
    ScopedConfigCleanup cleanup(testConfigRoot);
    std::filesystem::create_directories(testConfigRoot);

    const auto blockingFile = testConfigRoot / "not-a-directory";
    {
        std::ofstream out(blockingFile, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "block";
    }

    cupuacu::audio::AudioDevices::DeviceSelection selection;
    selection.hostApiIndex = 1;
    selection.outputDeviceIndex = 2;
    selection.inputDeviceIndex = 3;

    ScopedResolverOverride resolverOverride(
        cupuacu::persistence::AudioDevicePropertiesPersistence::Resolver{});

    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::save(
        blockingFile / "audio-device-properties.json", selection));
}

TEST_CASE("Audio device properties persistence load rejects directory and wrong JSON shapes",
          "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config-shapes";
    ScopedConfigCleanup cleanup(testConfigRoot);
    std::filesystem::create_directories(testConfigRoot);

    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                      testConfigRoot)
                      .has_value());

    const auto jsonPath = testConfigRoot / "audio-device-properties.json";
    {
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"(["not","an","object"])";
    }
    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                      jsonPath)
                      .has_value());

    {
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << R"({"version":"1","hostApiName":"Mock Host API","outputDeviceName":"Mock Output Device","inputDeviceName":"Mock Input Device"})";
    }
    REQUIRE_FALSE(cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                      jsonPath)
                      .has_value());
}

TEST_CASE("Audio device properties persistence tolerates resolver functions being absent",
          "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config-null-resolver";
    ScopedConfigCleanup cleanup(testConfigRoot);
    const auto propertiesPath = testConfigRoot / "audio-device-properties.json";

    cupuacu::audio::AudioDevices::DeviceSelection selection;
    selection.hostApiIndex = 4;
    selection.outputDeviceIndex = 5;
    selection.inputDeviceIndex = 6;

    ScopedResolverOverride resolverOverride(
        cupuacu::persistence::AudioDevicePropertiesPersistence::Resolver{});

    REQUIRE(cupuacu::persistence::AudioDevicePropertiesPersistence::save(
        propertiesPath, selection));

    nlohmann::json persistedJson;
    {
        std::ifstream in(propertiesPath, std::ios::binary);
        REQUIRE(in.good());
        in >> persistedJson;
    }
    REQUIRE(persistedJson.at("hostApiName").get<std::string>().empty());
    REQUIRE(persistedJson.at("outputDeviceName").get<std::string>().empty());
    REQUIRE(persistedJson.at("inputDeviceName").get<std::string>().empty());

    const auto loaded =
        cupuacu::persistence::AudioDevicePropertiesPersistence::load(
            propertiesPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->hostApiIndex == -1);
    REQUIRE(loaded->outputDeviceIndex == -1);
    REQUIRE(loaded->inputDeviceIndex == -1);
}

TEST_CASE("Audio device properties persistence default resolver round-trips empty device names",
          "[persistence]")
{
    const auto testConfigRoot =
        std::filesystem::temp_directory_path() / "cupuacu-test-config-default-resolver";
    ScopedConfigCleanup cleanup(testConfigRoot);
    const auto propertiesPath = testConfigRoot / "audio-device-properties.json";

    cupuacu::persistence::AudioDevicePropertiesPersistence::resetResolverForTesting();

    cupuacu::audio::AudioDevices::DeviceSelection selection;
    selection.hostApiIndex = -1;
    selection.outputDeviceIndex = -1;
    selection.inputDeviceIndex = -1;

    REQUIRE(cupuacu::persistence::AudioDevicePropertiesPersistence::save(
        propertiesPath, selection));

    nlohmann::json persistedJson;
    {
        std::ifstream in(propertiesPath, std::ios::binary);
        REQUIRE(in.good());
        in >> persistedJson;
    }
    REQUIRE(persistedJson.at("version").get<int>() == 1);
    REQUIRE(persistedJson.at("hostApiName").get<std::string>().empty());
    REQUIRE(persistedJson.at("outputDeviceName").get<std::string>().empty());
    REQUIRE(persistedJson.at("inputDeviceName").get<std::string>().empty());

    const auto loaded =
        cupuacu::persistence::AudioDevicePropertiesPersistence::load(
            propertiesPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->hostApiIndex == -1);
    REQUIRE(loaded->outputDeviceIndex == -1);
    REQUIRE(loaded->inputDeviceIndex == -1);
}
