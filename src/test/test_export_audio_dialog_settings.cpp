#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/ExportAudioDialogSettings.hpp"
#include "file/AudioExport.hpp"

TEST_CASE("preferred export audio dialog settings prefer per-tab memory",
          "[actions]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 2, 32);
    state.getActiveDocumentSession().currentFile = "/tmp/current.wav";

    auto currentFileSettings = cupuacu::file::defaultExportSettingsForPath(
        "current.wav", state.getActiveDocumentSession().document.getSampleFormat());
    auto rememberedSettings = cupuacu::file::defaultExportSettingsForPath(
        "remembered.flac",
        state.getActiveDocumentSession().document.getSampleFormat());
    REQUIRE(currentFileSettings.has_value());
    REQUIRE(rememberedSettings.has_value());

    state.getActiveDocumentSession().currentFileExportSettings =
        *currentFileSettings;
    state.tabs[0].lastExportAudioDialogSettings = *rememberedSettings;

    const auto resolved =
        cupuacu::actions::preferredExportAudioDialogSettings(&state);

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->container == rememberedSettings->container);
    REQUIRE(resolved->codec == rememberedSettings->codec);
    REQUIRE(resolved->subtype == rememberedSettings->subtype);
}

TEST_CASE("preferred export audio dialog settings fall back to current file settings",
          "[actions]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 2, 32);

    auto currentFileSettings = cupuacu::file::defaultExportSettingsForPath(
        "current.wav", state.getActiveDocumentSession().document.getSampleFormat());
    REQUIRE(currentFileSettings.has_value());

    state.getActiveDocumentSession().currentFileExportSettings =
        *currentFileSettings;

    const auto resolved =
        cupuacu::actions::preferredExportAudioDialogSettings(&state);

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->container == currentFileSettings->container);
    REQUIRE(resolved->codec == currentFileSettings->codec);
    REQUIRE(resolved->subtype == currentFileSettings->subtype);
}

TEST_CASE("remember last used export audio dialog settings stores per active tab",
          "[actions]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.emplace_back();
    state.activeTabIndex = 1;
    state.tabs[1].session.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                              44100, 2, 16);

    auto rememberedSettings = cupuacu::file::defaultExportSettingsForPath(
        "remembered.flac",
        state.tabs[1].session.document.getSampleFormat());
    REQUIRE(rememberedSettings.has_value());

    cupuacu::actions::rememberLastUsedExportAudioDialogSettings(
        &state, *rememberedSettings);

    REQUIRE_FALSE(state.tabs[0].lastExportAudioDialogSettings.has_value());
    REQUIRE(state.tabs[1].lastExportAudioDialogSettings.has_value());
    REQUIRE(state.tabs[1].lastExportAudioDialogSettings->container ==
            rememberedSettings->container);
    REQUIRE(state.tabs[1].lastExportAudioDialogSettings->codec ==
            rememberedSettings->codec);
    REQUIRE(state.tabs[1].lastExportAudioDialogSettings->subtype ==
            rememberedSettings->subtype);
}
