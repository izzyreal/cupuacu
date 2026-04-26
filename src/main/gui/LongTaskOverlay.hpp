#pragma once

#include "../LongTask.hpp"
#include "../State.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "Helpers.hpp"
#include "LabelPlanning.hpp"
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
    public:
        explicit LongTaskOverlay(State *stateToUse)
            : Component(stateToUse, "LongTaskOverlay")
        {
            setVisible(false);
            setInterceptMouseEnabled(true);
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
            if (title != lastTitle || detail != lastDetail ||
                progress != lastProgress)
            {
                lastTitle = title;
                lastDetail = detail;
                lastProgress = progress;
                setDirty();
            }
            else if (shouldBeVisible && !progress.has_value())
            {
                setDirty();
            }
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

            const int panelWidth =
                std::min(getWidth() - scaleUi(state, 40.0f),
                         scaleUi(state, 520.0f));
            const int panelHeight = scaleUi(state, 116.0f);
            const int panelX = (getWidth() - panelWidth) / 2;
            const int panelY = (getHeight() - panelHeight) / 2;
            const SDL_Rect panel{panelX, panelY, panelWidth, panelHeight};

            Helpers::fillRect(renderer, panel, SDL_Color{24, 24, 24, 245});
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            const SDL_FRect panelFrame = Helpers::rectToFRect(panel);
            SDL_RenderRect(renderer, &panelFrame);

            const int padding = scaleUi(state, 18.0f);
            const int titleFont =
                scaleFontPointSize(
                    state, std::max(1, static_cast<int>(state->menuFontSize)));
            const int detailFont =
                scaleFontPointSize(
                    state,
                    std::max(1, static_cast<int>(state->menuFontSize) - 8));
            const SDL_FRect titleRect{
                static_cast<float>(panel.x + padding),
                static_cast<float>(panel.y + padding),
                static_cast<float>(panel.w - padding * 2),
                static_cast<float>(scaleUi(state, 38.0f))};
            const SDL_FRect detailRect{
                static_cast<float>(panel.x + padding),
                static_cast<float>(panel.y + padding + scaleUi(state, 44.0f)),
                static_cast<float>(panel.w - padding * 2),
                static_cast<float>(scaleUi(state, 30.0f))};

            renderEllipsizedText(renderer, state->longTask.title, titleFont,
                                  titleRect, true);
            renderEllipsizedText(renderer, state->longTask.detail, detailFont,
                                  detailRect, true);

            const int barX = panel.x + padding;
            const int barY = panel.y + panel.h - padding -
                             scaleUi(state, 10.0f);
            const int barW = panel.w - padding * 2;
            const int barH = scaleUi(state, 6.0f);
            const SDL_Rect track{barX, barY, barW, barH};
            Helpers::fillRect(renderer, track, SDL_Color{70, 70, 70, 255});

            if (!state->longTask.progress.has_value())
            {
                const int fillW = std::max(scaleUi(state, 40.0f), barW / 4);
                const int travelW = std::max(1, barW + fillW);
                const auto phase =
                    static_cast<int>((SDL_GetTicks() / 8u) %
                                     static_cast<Uint64>(travelW));
                const int fillX = barX - fillW + phase;
                const SDL_Rect fill{
                    std::clamp(fillX, barX, barX + barW),
                    barY,
                    std::max(0, std::min(fillX + fillW, barX + barW) -
                                    std::clamp(fillX, barX, barX + barW)),
                    barH};
                Helpers::fillRect(renderer, fill, SDL_Color{0, 185, 0, 255});
            }
            else
            {
                const double progress =
                    std::clamp(*state->longTask.progress, 0.0, 1.0);
                const SDL_Rect fill{
                    barX, barY,
                    static_cast<int>(std::lround(
                        static_cast<double>(barW) * progress)),
                    barH};
                Helpers::fillRect(renderer, fill, SDL_Color{0, 185, 0, 255});
            }
        }

    private:
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
    };
} // namespace cupuacu::gui
