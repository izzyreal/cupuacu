#pragma once

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/MainView.hpp"

#include <memory>

namespace cupuacu::test
{
    struct SessionUi
    {
        std::unique_ptr<cupuacu::gui::MainView> mainView;
    };

    inline SessionUi createSessionUi(cupuacu::State *state,
                                     const int64_t frameCount,
                                     const bool withAudioDevices = false,
                                     const int channels = 2,
                                     const int sampleRate = 44100,
                                     const int windowWidth = 800,
                                     const int windowHeight = 400,
                                     const int mainViewHeight = 300)
    {
        ensureSdlTtfInitialized();

        auto &session = state->activeDocumentSession;
        session.document.initialize(cupuacu::SampleFormat::FLOAT32, sampleRate,
                                    channels, frameCount);

        if (withAudioDevices)
        {
            state->audioDevices =
                std::make_shared<cupuacu::audio::AudioDevices>(false);
        }

        state->mainDocumentSessionWindow =
            std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                state, &session, "test", windowWidth, windowHeight,
                SDL_WINDOW_HIDDEN);

        SessionUi ui{};
        ui.mainView = std::make_unique<cupuacu::gui::MainView>(state);
        state->mainView = ui.mainView.get();
        ui.mainView->setBounds(0, 0, windowWidth, mainViewHeight);
        return ui;
    }
} // namespace cupuacu::test
