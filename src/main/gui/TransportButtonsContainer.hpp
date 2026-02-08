#pragma once

#include "Button.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "Helpers.hpp"
#include "TextButton.hpp"

#include "../actions/Play.hpp"
#include "../actions/Record.hpp"
#include "playback/PlaybackRange.hpp"

#include <algorithm>
#include <optional>

namespace cupuacu::gui
{
    class TransportButtonsContainer : public Component
    {
    public:
        explicit TransportButtonsContainer(State *state)
            : Component(state, "TransportButtonsContainer")
        {
            playButton =
                emplaceChild<TextButton>(state, "Play", ButtonType::Momentary);
            stopButton =
                emplaceChild<TextButton>(state, "Stop", ButtonType::Momentary);
            recordButton = emplaceChild<TextButton>(state, "Record",
                                                    ButtonType::Momentary);
            loopButton =
                emplaceChild<TextButton>(state, "Loop", ButtonType::Toggle);

            playButton->setOnPress(
                [state]
                {
                    if (state->audioDevices->isPlaying())
                    {
                        actions::requestStop(state);
                        return;
                    }
                    actions::play(state);
                });

            stopButton->setOnPress(
                [state]
                {
                    actions::requestStop(state);
                });

            recordButton->setOnPress(
                [state]
                {
                    actions::record(state);
                });

            loopButton->setOnToggle(
                [state](const bool toggled)
                {
                    state->loopPlaybackEnabled = toggled;
                    if (!state->audioDevices || !state->audioDevices->isPlaying())
                    {
                        return;
                    }

                    auto &session = state->activeDocumentSession;
                    auto &viewState =
                        state->mainDocumentSessionWindow->getViewState();
                    const auto range =
                        cupuacu::playback::computeRangeForLiveUpdate(
                            session, state->loopPlaybackEnabled,
                            state->playbackRangeStart, state->playbackRangeEnd);

                    cupuacu::audio::UpdatePlayback updateMsg{};
                    updateMsg.startPos = range.start;
                    updateMsg.endPos = range.end;
                    updateMsg.loopEnabled = state->loopPlaybackEnabled;
                    updateMsg.selectionIsActive = session.selection.isActive();
                    updateMsg.selectedChannels = viewState.selectedChannels;
                    state->audioDevices->enqueue(updateMsg);
                });
            loopButton->setToggled(state->loopPlaybackEnabled);
        }

        void resized() override
        {
            const SDL_Rect bounds = getLocalBounds();
            const int padding =
                std::max(4, static_cast<int>(8 / state->pixelScale));
            const int gap =
                std::max(4, static_cast<int>(6 / state->pixelScale));

            const int contentW = std::max(0, bounds.w - 2 * padding);
            const int contentH = std::max(0, bounds.h - 2 * padding);
            const int buttonW = std::max(0, (contentW - 3 * gap) / 4);

            playButton->setBounds(padding, padding, buttonW, contentH);
            stopButton->setBounds(padding + buttonW + gap, padding, buttonW,
                                  contentH);
            recordButton->setBounds(padding + (buttonW + gap) * 2, padding,
                                    buttonW, contentH);
            loopButton->setBounds(padding + (buttonW + gap) * 3, padding,
                                  buttonW, contentH);
        }

        void timerCallback() override
        {
            if (!state->audioDevices)
            {
                return;
            }

            const bool isPlaying = state->audioDevices->isPlaying();
            const bool isRecording = state->audioDevices->isRecording();

            const std::optional<SDL_Color> playColor =
                isPlaying ? std::optional<SDL_Color>{SDL_Color{52, 132, 67, 255}}
                          : std::nullopt;
            const std::optional<SDL_Color> recordColor =
                isRecording
                    ? std::optional<SDL_Color>{SDL_Color{168, 80, 46, 255}}
                    : std::nullopt;

            playButton->setForcedFillColor(playColor);
            recordButton->setForcedFillColor(recordColor);
            loopButton->setToggled(state->loopPlaybackEnabled);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
        }

    private:
        TextButton *playButton;
        TextButton *stopButton;
        TextButton *recordButton;
        TextButton *loopButton;
    };
} // namespace cupuacu::gui
