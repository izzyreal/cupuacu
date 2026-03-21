#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "SelectedChannels.hpp"
#include "audio/AudioDevices.hpp"
#include "audio/AudioMessage.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/TextButton.hpp"
#include "gui/TransportButtonsContainer.hpp"
#include "gui/VuMeter.hpp"

#include <memory>
#include <string_view>

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
    meter.pushPeakForChannel(0.8f, 1);
    meter.pushPeakForChannel(0.5f, 9);
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
