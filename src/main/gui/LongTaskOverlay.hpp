#pragma once

#include "../LongTask.hpp"
#include "../State.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "Helpers.hpp"
#include "LabelPlanning.hpp"
#include "TextButton.hpp"
#include "UiScale.hpp"
#include "text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace cupuacu::gui
{
    class LongTaskOverlay : public Component
    {
        struct Layout
        {
            SDL_Rect panel{0, 0, 0, 0};
            SDL_FRect titleRect{0, 0, 0, 0};
            SDL_FRect detailRect{0, 0, 0, 0};
            SDL_Rect progressTrack{0, 0, 0, 0};
            SDL_Rect cancelButtonRect{0, 0, 0, 0};
        };

    public:
        explicit LongTaskOverlay(State *stateToUse)
            : Component(stateToUse, "LongTaskOverlay")
        {
            setVisible(false);
            setInterceptMouseEnabled(true);
            cancelButton = emplaceChild<TextButton>(state, "Cancel");
            cancelButton->setTriggerOnMouseUp(true);
            cancelButton->setOnPress(
                [this]()
                {
                    if (state)
                    {
                        cupuacu::requestLongTaskCancel(state);
                        syncToState();
                    }
                });
            cancelButton->setVisible(false);
        }

        void timerCallback() override
        {
            syncToState();
        }

        void syncToState()
        {
            const bool shouldBeVisible = state && state->longTask.active;
            if (isVisible() != shouldBeVisible)
            {
                setVisible(shouldBeVisible);
            }

            const std::string title =
                shouldBeVisible ? state->longTask.title : "";
            const std::string detail =
                shouldBeVisible ? state->longTask.detail : "";
            const auto progress =
                shouldBeVisible ? state->longTask.progress : std::nullopt;
            const bool cancellable =
                shouldBeVisible && state->longTask.cancellable;
            const bool cancelRequested =
                shouldBeVisible && state->longTask.cancelRequested;
            if (title != lastTitle || detail != lastDetail ||
                progress != lastProgress ||
                cancellable != lastCancellable ||
                cancelRequested != lastCancelRequested)
            {
                lastTitle = title;
                lastDetail = detail;
                lastProgress = progress;
                lastCancellable = cancellable;
                lastCancelRequested = cancelRequested;
                setDirty();
            }
            else if (shouldBeVisible && !progress.has_value())
            {
                setDirty();
            }

            if (cancelButton)
            {
                cancelButton->setVisible(cancellable);
                cancelButton->setEnabled(!cancelRequested);
                cancelButton->setText(cancelRequested ? "Canceling..." : "Cancel");
            }
            resized();
        }

        bool mouseDown(const MouseEvent &) override
        {
            return true;
        }

        bool mouseUp(const MouseEvent &) override
        {
            return true;
        }

        bool mouseMove(const MouseEvent &) override
        {
            return true;
        }

        bool mouseWheel(const MouseEvent &) override
        {
            return true;
        }

        bool shouldCaptureMouse() const override
        {
            return false;
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            if (!state || !state->longTask.active)
            {
                return;
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            Helpers::fillRect(renderer, getLocalBounds(),
                              SDL_Color{0, 0, 0, 110});

            const auto layout = computeLayout();

            Helpers::fillRect(renderer, layout.panel,
                              SDL_Color{24, 24, 24, 245});
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            const SDL_FRect panelFrame = Helpers::rectToFRect(layout.panel);
            SDL_RenderRect(renderer, &panelFrame);

            const int titleFont =
                scaleFontPointSize(
                    state, std::max(1, static_cast<int>(state->menuFontSize)));
            const int detailFont =
                scaleFontPointSize(
                    state,
                    std::max(1, static_cast<int>(state->menuFontSize) - 8));
            renderEllipsizedText(renderer, state->longTask.title, titleFont,
                                 layout.titleRect, true);
            renderEllipsizedText(renderer, state->longTask.detail, detailFont,
                                 layout.detailRect, true);

            Helpers::fillRect(renderer, layout.progressTrack,
                              SDL_Color{70, 70, 70, 255});

            if (!state->longTask.progress.has_value())
            {
                const int fillW = std::max(scaleUi(state, 40.0f),
                                           layout.progressTrack.w / 4);
                const int travelW =
                    std::max(1, layout.progressTrack.w + fillW);
                const auto phase =
                    static_cast<int>((SDL_GetTicks() / 8u) %
                                     static_cast<Uint64>(travelW));
                const int fillX = layout.progressTrack.x - fillW + phase;
                const SDL_Rect fill{
                    std::clamp(fillX, layout.progressTrack.x,
                               layout.progressTrack.x + layout.progressTrack.w),
                    layout.progressTrack.y,
                    std::max(
                        0,
                        std::min(fillX + fillW,
                                 layout.progressTrack.x + layout.progressTrack.w) -
                            std::clamp(fillX, layout.progressTrack.x,
                                       layout.progressTrack.x +
                                           layout.progressTrack.w)),
                    layout.progressTrack.h};
                Helpers::fillRect(renderer, fill, SDL_Color{0, 185, 0, 255});
            }
            else
            {
                const double progress =
                    std::clamp(*state->longTask.progress, 0.0, 1.0);
                const SDL_Rect fill{
                    layout.progressTrack.x, layout.progressTrack.y,
                    static_cast<int>(std::lround(
                        static_cast<double>(layout.progressTrack.w) * progress)),
                    layout.progressTrack.h};
                Helpers::fillRect(renderer, fill, SDL_Color{0, 185, 0, 255});
            }
        }

        void resized() override
        {
            if (!cancelButton || !state)
            {
                return;
            }

            cancelButton->setBounds(computeLayout().cancelButtonRect);
        }

    private:
        [[nodiscard]] Layout computeLayout() const
        {
            Layout layout{};
            if (!state)
            {
                return layout;
            }

            const int outerMargin = scaleUi(state, 40.0f);
            const int padding = scaleUi(state, 18.0f);
            const int titleHeight = scaleUi(state, 38.0f);
            const int detailHeight = scaleUi(state, 30.0f);
            const int titleToDetailGap = scaleUi(state, 8.0f);
            const int detailToBarGap = scaleUi(state, 18.0f);
            const int buttonWidth = scaleUi(state, 120.0f);
            const int buttonHeight = scaleUi(state, 34.0f);
            const int buttonGap = scaleUi(state, 12.0f);
            const int barHeight = scaleUi(state, 6.0f);

            const int panelWidth =
                std::min(getWidth() - outerMargin, scaleUi(state, 520.0f));
            const int textBlockHeight =
                titleHeight + titleToDetailGap + detailHeight;
            const int footerHeight = state->longTask.cancellable
                                         ? detailToBarGap + barHeight + buttonGap +
                                               buttonHeight
                                         : detailToBarGap + barHeight;
            const int panelHeight = padding * 2 + textBlockHeight + footerHeight;
            const int panelX = (getWidth() - panelWidth) / 2;
            const int panelY = (getHeight() - panelHeight) / 2;

            layout.panel = {panelX, panelY, panelWidth, panelHeight};
            layout.titleRect = {
                static_cast<float>(panelX + padding),
                static_cast<float>(panelY + padding),
                static_cast<float>(panelWidth - padding * 2),
                static_cast<float>(titleHeight)};
            layout.detailRect = {
                static_cast<float>(panelX + padding),
                static_cast<float>(panelY + padding + titleHeight + titleToDetailGap),
                static_cast<float>(panelWidth - padding * 2),
                static_cast<float>(detailHeight)};

            const int progressY =
                panelY + padding + textBlockHeight + detailToBarGap;
            layout.progressTrack = {panelX + padding, progressY,
                                    panelWidth - padding * 2, barHeight};

            layout.cancelButtonRect = {
                panelX + panelWidth - padding - buttonWidth,
                progressY + barHeight + buttonGap,
                buttonWidth,
                buttonHeight};
            return layout;
        }

        static void renderEllipsizedText(SDL_Renderer *renderer,
                                         const std::string &text,
                                         const int pointSize,
                                         const SDL_FRect &rect,
                                         const bool shouldCenterHorizontally)
        {
            const int availableWidth =
                std::max(0, static_cast<int>(std::floor(rect.w)));
            const std::string renderedText = ellipsizeTextToWidth(
                text, availableWidth,
                [pointSize](const std::string &value)
                {
                    return cupuacu::gui::measureText(value, pointSize).first;
                });
            renderText(renderer, renderedText, static_cast<std::uint8_t>(pointSize),
                       rect, shouldCenterHorizontally);
        }

        std::string lastTitle;
        std::string lastDetail;
        std::optional<double> lastProgress;
        bool lastCancellable = false;
        bool lastCancelRequested = false;
        TextButton *cancelButton = nullptr;
    };
} // namespace cupuacu::gui
