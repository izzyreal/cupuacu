#pragma once

#include "Button.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "Helpers.hpp"
#include "TextButton.hpp"

#include "../actions/Play.hpp"
#include "../actions/Record.hpp"

#include <algorithm>

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
            const int buttonW = std::max(0, (contentW - 2 * gap) / 3);

            playButton->setBounds(padding, padding, buttonW, contentH);
            stopButton->setBounds(padding + buttonW + gap, padding, buttonW,
                                  contentH);
            recordButton->setBounds(padding + (buttonW + gap) * 2, padding,
                                    buttonW, contentH);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
        }

    private:
        TextButton *playButton;
        TextButton *stopButton;
        TextButton *recordButton;
    };
} // namespace cupuacu::gui
