#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "TestPaths.hpp"
#include "SelectedChannels.hpp"
#include "audio/AudioDevices.hpp"
#include "audio/AudioMessage.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DisplaySettingsWindow.hpp"
#include "gui/Label.hpp"
#include "gui/Ruler.hpp"
#include "gui/TextButton.hpp"
#include "gui/TransportButtonsContainer.hpp"
#include "gui/VuMeterContainer.hpp"
#include "gui/VuMeter.hpp"
#include "gui/VuMeterModel.hpp"
#include "gui/Window.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
    cupuacu::gui::TextButton *findTextButton(cupuacu::gui::Component *root,
                                             const std::string_view name)
    {
        if (root == nullptr)
        {
            return nullptr;
        }

        if (root->getComponentName() == name)
        {
            return dynamic_cast<cupuacu::gui::TextButton *>(root);
        }

        for (const auto &child : root->getChildren())
        {
            if (auto *found = findTextButton(child.get(), name))
            {
                return found;
            }
        }

        return nullptr;
    }

    cupuacu::gui::MouseEvent leftMouseDownAt(const int x = 1, const int y = 1)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, x, y, static_cast<float>(x),
            static_cast<float>(y), 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }

    class RootComponent : public cupuacu::gui::Component
    {
    public:
        explicit RootComponent(cupuacu::State *state)
            : Component(state, "Root")
        {
        }
    };

    template <typename T>
    T *findFirstRecursive(cupuacu::gui::Component *root)
    {
        if (!root)
        {
            return nullptr;
        }

        if (auto *typed = dynamic_cast<T *>(root))
        {
            return typed;
        }

        for (const auto &child : root->getChildren())
        {
            if (auto *found = findFirstRecursive<T>(child.get()))
            {
                return found;
            }
        }

        return nullptr;
    }

    template <typename T>
    void collectChildrenRecursive(cupuacu::gui::Component *root,
                                  std::vector<T *> &out)
    {
        if (!root)
        {
            return;
        }

        if (auto *typed = dynamic_cast<T *>(root))
        {
            out.push_back(typed);
        }

        for (const auto &child : root->getChildren())
        {
            collectChildrenRecursive<T>(child.get(), out);
        }
    }

    std::unique_ptr<cupuacu::gui::Window> createWindowWithVuMeterContainer(
        cupuacu::State *state, cupuacu::gui::VuMeterContainer *&outContainer)
    {
        cupuacu::test::ensureSdlTtfInitialized();
        auto window = std::make_unique<cupuacu::gui::Window>(
            state, "vu-meter-test", 480, 180, SDL_WINDOW_HIDDEN);
        auto root = std::make_unique<RootComponent>(state);
        root->setBounds(0, 0, 480, 180);
        outContainer = root->emplaceChild<cupuacu::gui::VuMeterContainer>(state);
        outContainer->setBounds(0, 0, 480, 120);
        window->setRootComponent(std::move(root));
        return window;
    }
} // namespace

TEST_CASE("Transport buttons container drives record stop play-stop and loop state",
          "[gui][audio]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    auto &doc = state.getActiveDocumentSession().document;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 16);

    cupuacu::gui::TransportButtonsContainer container(&state);
    container.setBounds(0, 0, 400, 48);
    container.resized();

    auto *playButton = findTextButton(&container, "TextButton:Play");
    auto *stopButton = findTextButton(&container, "TextButton:Stop");
    auto *recordButton = findTextButton(&container, "TextButton:Record");
    auto *loopButton = findTextButton(&container, "TextButton:Loop");

    REQUIRE(playButton != nullptr);
    REQUIRE(stopButton != nullptr);
    REQUIRE(recordButton != nullptr);
    REQUIRE(loopButton != nullptr);

    REQUIRE(recordButton->mouseDown(leftMouseDownAt()));
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isRecording());

    REQUIRE(stopButton->mouseDown(leftMouseDownAt()));
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isRecording());

    cupuacu::audio::Play playMessage{};
    playMessage.document = &doc;
    playMessage.startPos = 0;
    playMessage.endPos = 8;
    playMessage.loopEnabled = false;
    playMessage.selectionIsActive = false;
    playMessage.selectedChannels = cupuacu::SelectedChannels::BOTH;
    playMessage.vuMeter = nullptr;
    state.audioDevices->enqueue(std::move(playMessage));
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isPlaying());

    REQUIRE(playButton->mouseDown(leftMouseDownAt()));
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isPlaying());

    REQUIRE(loopButton->mouseDown(leftMouseDownAt()));
    REQUIRE(state.loopPlaybackEnabled);
    REQUIRE(loopButton->isToggled());

    state.loopPlaybackEnabled = false;
    loopButton->setToggled(true);
    container.timerCallback();
    REQUIRE_FALSE(loopButton->isToggled());
}

TEST_CASE("Vu meter timer reacts to pushed peaks and decay without SDL rendering",
          "[gui][audio]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::VuMeter meter(&state);
    meter.setVisible(true);

    REQUIRE_FALSE(meter.isDirty());
    meter.timerCallback();
    REQUIRE_FALSE(meter.isDirty());

    meter.setNumChannels(2);
    meter.pushMeterFrameForChannel({.peak = 0.8f, .rms = 0.5f}, 1);
    meter.pushMeterFrameForChannel({.peak = 0.5f, .rms = 0.25f}, 9);
    meter.setPeaksPushed();
    meter.timerCallback();
    REQUIRE(meter.isDirty());
}

TEST_CASE("Vu meter timer leaves an empty decay pass clean",
          "[gui][audio]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::VuMeter meter(&state);
    meter.setVisible(true);

    meter.startDecay();
    meter.timerCallback();

    REQUIRE_FALSE(meter.isDirty());
}

TEST_CASE("Vu meter scale config exposes the expected ruler labels",
          "[gui][audio]")
{
    const auto k14 = cupuacu::gui::getVuMeterScaleConfig(
        cupuacu::gui::VuMeterScale::K14);
    REQUIRE(std::find(k14.labels.begin(), k14.labels.end(), "-20") !=
            k14.labels.end());
    REQUIRE(std::find(k14.labels.begin(), k14.labels.end(), "0") !=
            k14.labels.end());
    REQUIRE(k14.endLabel == "+14");
    REQUIRE(k14.longTickSubdivisions == Catch::Approx(2.0f));

    const auto peak = cupuacu::gui::getVuMeterScaleConfig(
        cupuacu::gui::VuMeterScale::PeakDbfs);
    REQUIRE(std::find(peak.labels.begin(), peak.labels.end(), "-72") !=
            peak.labels.end());
    REQUIRE(std::find(peak.labels.begin(), peak.labels.end(), "0") ==
            peak.labels.end());
    REQUIRE(peak.endLabel == "0");
    REQUIRE(peak.longTickSubdivisions == Catch::Approx(3.0f));
}

TEST_CASE("Display settings helpers map pixel scale values consistently",
          "[gui][audio]")
{
    REQUIRE(cupuacu::gui::displayPixelScaleOptionLabels() ==
            std::vector<std::string>{"1", "2", "4"});
    REQUIRE(cupuacu::gui::displayPixelScaleToIndex(1) == 0);
    REQUIRE(cupuacu::gui::displayPixelScaleToIndex(2) == 1);
    REQUIRE(cupuacu::gui::displayPixelScaleToIndex(4) == 2);
    REQUIRE(cupuacu::gui::displayPixelScaleFromIndex(0) == 1);
    REQUIRE(cupuacu::gui::displayPixelScaleFromIndex(1) == 2);
    REQUIRE(cupuacu::gui::displayPixelScaleFromIndex(2) == 4);
}

TEST_CASE("Display settings pixel scale helper rescales samples-per-pixel",
          "[gui][audio]")
{
    REQUIRE(cupuacu::gui::adjustSamplesPerPixelForDisplayPixelScaleChange(
                12.0, 1, 2) == Catch::Approx(24.0));
    REQUIRE(cupuacu::gui::adjustSamplesPerPixelForDisplayPixelScaleChange(
                12.0, 2, 1) == Catch::Approx(6.0));
    REQUIRE(cupuacu::gui::adjustSamplesPerPixelForDisplayPixelScaleChange(
                12.0, 2, 4) == Catch::Approx(24.0));
}

TEST_CASE("Vu meter model uses RMS for K scales and peak for Peak dBFS",
          "[gui][audio]")
{
    cupuacu::gui::VuMeterModel model{};
    model.setNumChannels(1);

    model.setScale(cupuacu::gui::VuMeterScale::PeakDbfs);
    const auto peakDisplay = model.advanceChannel(
        0, {.peak = 1.0f, .rms = 0.25f}, false);

    model.setScale(cupuacu::gui::VuMeterScale::K20);
    const auto kDisplay = model.advanceChannel(
        0, {.peak = 1.0f, .rms = 0.25f}, false);

    REQUIRE(peakDisplay.level > kDisplay.level);
    REQUIRE(peakDisplay.hold ==
            Catch::Approx(cupuacu::gui::normalizePeakForVuMeter(
                1.0f, cupuacu::gui::VuMeterScale::PeakDbfs)));
    REQUIRE(kDisplay.hold ==
            Catch::Approx(cupuacu::gui::normalizePeakForVuMeter(
                0.25f, cupuacu::gui::VuMeterScale::K20)));
}

TEST_CASE("Vu meter model resets cached state when scale changes",
          "[gui][audio]")
{
    cupuacu::gui::VuMeterModel model{};
    model.setNumChannels(1);
    model.setScale(cupuacu::gui::VuMeterScale::PeakDbfs);

    const auto first = model.advanceChannel(0, {.peak = 1.0f, .rms = 1.0f}, false);
    REQUIRE(first.level > 0.0f);

    model.setScale(cupuacu::gui::VuMeterScale::K14);
    const auto afterScaleChange =
        model.advanceChannel(0, {.peak = 0.0f, .rms = 0.0f}, false);

    REQUIRE(afterScaleChange.level == Catch::Approx(0.0f));
    REQUIRE(afterScaleChange.hold == Catch::Approx(0.0f));
}

TEST_CASE("Vu meter model hold decays after the hold period",
          "[gui][audio]")
{
    cupuacu::gui::VuMeterModel model{};
    model.setNumChannels(1);
    model.setScale(cupuacu::gui::VuMeterScale::PeakDbfs);

    const auto initial = model.advanceChannel(0, {.peak = 1.0f, .rms = 1.0f}, false);
    float lastHold = initial.hold;
    for (int i = 0; i < 31; ++i)
    {
        lastHold = model.advanceChannel(0, {.peak = 0.0f, .rms = 0.0f}, false).hold;
    }

    REQUIRE(lastHold < initial.hold);
}
