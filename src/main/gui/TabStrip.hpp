#pragma once

#include "../State.hpp"
#include "../actions/DocumentTabs.hpp"

#include "Colors.hpp"
#include "Helpers.hpp"
#include "Label.hpp"
#include "RoundedRect.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cupuacu::gui
{
    namespace
    {
        constexpr SDL_Color kTabBaseFill{78, 78, 78, 255};
        constexpr SDL_Color kTabHoverFill{96, 96, 96, 255};
        constexpr SDL_Color kTabPressedFill{66, 92, 134, 255};
        constexpr SDL_Color kTabActiveFill{74, 110, 170, 255};
        constexpr SDL_Color kTabCloseHoverFill{52, 52, 52, 128};
    }

    class TabStripTab : public Component
    {
    public:
        TabStripTab(State *stateToUse, const std::string &titleToUse,
                    const int tabIndexToUse)
            : Component(stateToUse, "TabStripTab:" + titleToUse),
              tabIndex(tabIndexToUse), title(titleToUse)
        {
            titleLabel = emplaceChild<Label>(state, titleToUse);
            titleLabel->setInterceptMouseEnabled(false);
            titleLabel->setFontSize(state->menuFontSize - 6);
        }

        void setActive(const bool shouldBeActive)
        {
            if (active == shouldBeActive)
            {
                return;
            }
            active = shouldBeActive;
            setDirty();
        }

        void setEnabledState(const bool shouldBeEnabled)
        {
            if (enabled == shouldBeEnabled)
            {
                return;
            }
            enabled = shouldBeEnabled;
            setDirty();
        }

        void setOnSelect(std::function<void(int)> onSelectToUse)
        {
            onSelect = std::move(onSelectToUse);
        }

        void setOnClose(std::function<void(int)> onCloseToUse)
        {
            onClose = std::move(onCloseToUse);
        }

        void setTitle(const std::string &titleToUse)
        {
            if (title == titleToUse)
            {
                return;
            }

            title = titleToUse;
            titleLabel->setText(title);
            setDirty();
        }

        void setTooltipText(const std::string &tooltipTextToUse)
        {
            tooltipText = tooltipTextToUse;
        }

        std::string getTooltipText() const override
        {
            return tooltipText;
        }

        void resized() override
        {
            const SDL_Rect closeRect = getCloseBounds();
            const int titlePadding = scaleUi(state, 12.0f);
            titleLabel->setBounds(
                titlePadding, 0,
                std::max(0, closeRect.x - titlePadding * 2), getHeight());
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            SDL_BlendMode previousBlendMode = SDL_BLENDMODE_NONE;
            SDL_GetRenderDrawBlendMode(renderer, &previousBlendMode);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

            const SDL_FRect outer = getLocalBoundsF();
            const float radius = scaleUiF(state, 8.0f);
            SDL_Color fill = kTabBaseFill;

            if (pressTarget != PressTarget::None &&
                     pointerInsideWhilePressed)
            {
                fill = kTabPressedFill;
            }
            else if (active)
            {
                fill = kTabActiveFill;
            }
            else if (isMouseOver())
            {
                fill = kTabHoverFill;
            }

            drawRoundedRect(renderer, outer, radius, Colors::border);

            SDL_FRect inner = outer;
            inner.x += 1.0f;
            inner.y += 1.0f;
            inner.w -= 2.0f;
            inner.h -= 2.0f;
            drawRoundedRect(renderer, inner, std::max(0.0f, radius - 1.0f),
                            fill);
            drawCloseIcon(renderer, getCloseBounds());

            SDL_SetRenderDrawBlendMode(renderer, previousBlendMode);
        }

        bool mouseDown(const MouseEvent &e) override
        {
            if (!enabled)
            {
                return false;
            }

            if (!containsLocalPoint(e.mouseXi, e.mouseYi))
            {
                return false;
            }

            const bool clickingClose =
                isInsideRect(getCloseBounds(), e.mouseXi, e.mouseYi);

            // Active-tab body clicks should be inert: keep the active fill and
            // avoid dispatching a redundant select action. The click is still
            // consumed so it does not fall through to anything underneath.
            if (active && !clickingClose)
            {
                return true;
            }

            pressTarget =
                clickingClose ? PressTarget::Close : PressTarget::Body;
            pointerInsideWhilePressed = true;
            setDirty();
            return true;
        }

        bool mouseMove(const MouseEvent &e) override
        {
            const bool nextCloseHovered =
                containsLocalPoint(e.mouseXi, e.mouseYi) &&
                isInsideRect(getCloseBounds(), e.mouseXi, e.mouseYi);
            const bool hoverChanged = nextCloseHovered != closeHovered;
            if (hoverChanged)
            {
                closeHovered = nextCloseHovered;
                setDirty();
            }

            if (pressTarget == PressTarget::None)
            {
                return hoverChanged;
            }

            const bool insidePressedTarget =
                pressTarget == PressTarget::Close
                    ? isInsideRect(getCloseBounds(), e.mouseXi, e.mouseYi)
                    : containsLocalPoint(e.mouseXi, e.mouseYi) &&
                          !isInsideRect(getCloseBounds(), e.mouseXi, e.mouseYi);

            if (insidePressedTarget != pointerInsideWhilePressed)
            {
                pointerInsideWhilePressed = insidePressedTarget;
                setDirty();
            }

            return true;
        }

        bool mouseUp(const MouseEvent &e) override
        {
            if (pressTarget == PressTarget::None)
            {
                return false;
            }

            const PressTarget releasedTarget = pressTarget;
            const bool shouldTrigger =
                releasedTarget == PressTarget::Close
                    ? isInsideRect(getCloseBounds(), e.mouseXi, e.mouseYi)
                    : containsLocalPoint(e.mouseXi, e.mouseYi) &&
                          !isInsideRect(getCloseBounds(), e.mouseXi, e.mouseYi);

            pressTarget = PressTarget::None;
            pointerInsideWhilePressed = false;
            setDirty();

            if (!enabled || !shouldTrigger)
            {
                return true;
            }

            const int index = tabIndex;
            if (releasedTarget == PressTarget::Close)
            {
                if (onClose)
                {
                    onClose(index);
                }
            }
            else if (onSelect)
            {
                onSelect(index);
            }

            return true;
        }

        void mouseEnter() override
        {
            setDirty();
        }

        void mouseLeave() override
        {
            closeHovered = false;
            setDirty();
        }

    private:
        enum class PressTarget
        {
            None,
            Body,
            Close
        };

        int tabIndex = 0;
        bool active = false;
        bool enabled = true;
        bool closeHovered = false;
        bool pointerInsideWhilePressed = false;
        PressTarget pressTarget = PressTarget::None;
        std::string title;
        std::string tooltipText;
        Label *titleLabel = nullptr;
        std::function<void(int)> onSelect;
        std::function<void(int)> onClose;

        bool containsLocalPoint(const int x, const int y) const
        {
            return x >= 0 && y >= 0 && x < getWidth() && y < getHeight();
        }

        static bool isInsideRect(const SDL_Rect &rect, const int x, const int y)
        {
            return x >= rect.x && y >= rect.y &&
                   x < rect.x + rect.w && y < rect.y + rect.h;
        }

        SDL_Rect getCloseBounds() const
        {
            const int horizontalPadding = scaleUi(state, 4.0f);
            const int closeWidth =
                std::max(1, getHeight() - horizontalPadding * 2);
            return {
                std::max(horizontalPadding,
                         getWidth() - closeWidth - horizontalPadding),
                std::max(0, (getHeight() - closeWidth) / 2),
                closeWidth,
                closeWidth};
        }

        static float snapToPixelGrid(const State *state, const float value)
        {
            const float step = 1.0f /
                               std::max(1, static_cast<int>(
                                               state ? state->pixelScale : 1));
            return std::round(value / step) * step;
        }

        void drawCloseIcon(SDL_Renderer *renderer, const SDL_Rect &closeRect) const
        {
            // Use the center of the covered pixel area so the icon rasterizes
            // symmetrically for both odd and even close-button sizes.
            const float centerX = snapToPixelGrid(
                state, closeRect.x + (closeRect.w - 1) / 2.0f);
            const float centerY = snapToPixelGrid(
                state, closeRect.y + (closeRect.h - 1) / 2.0f);
            const float opticalOffsetX = 0.25f;
            const float opticalOffsetY = 0.35f;
            const float iconCenterX =
                snapToPixelGrid(state, centerX + opticalOffsetX);
            const float iconCenterY =
                snapToPixelGrid(state, centerY + opticalOffsetY);
            const float hoverDiameter =
                snapToPixelGrid(state, std::max(
                                           1.0f, static_cast<float>(
                                                     std::round(closeRect.w *
                                                                0.8f))));

            if (closeHovered ||
                (pressTarget == PressTarget::Close && pointerInsideWhilePressed))
            {
                const SDL_FRect hoverCircle{
                    centerX - hoverDiameter / 2.0f,
                    centerY - hoverDiameter / 2.0f,
                    hoverDiameter,
                    hoverDiameter};
                drawRoundedRect(renderer, hoverCircle, hoverCircle.w / 2.0f,
                                kTabCloseHoverFill);
            }

            const float iconSize =
                snapToPixelGrid(state, std::max(
                                           1.0f, static_cast<float>(
                                                     std::round(hoverDiameter *
                                                                0.72f))));
            const float thickness = snapToPixelGrid(
                state, std::max(1.0f, scaleUiF(state, 2.0f)));
            const float inset = snapToPixelGrid(
                state, std::max(scaleUiF(state, 1.5f), iconSize * 0.22f));
            const float halfSpan = snapToPixelGrid(
                state, std::max(0.0f, iconSize / 2.0f - inset));

            drawThickLine(renderer, iconCenterX - halfSpan,
                          iconCenterY - halfSpan, iconCenterX + halfSpan,
                          iconCenterY + halfSpan, thickness,
                          Colors::white);
            drawThickLine(renderer, iconCenterX - halfSpan,
                          iconCenterY + halfSpan, iconCenterX + halfSpan,
                          iconCenterY - halfSpan, thickness,
                          Colors::white);
            Helpers::fillRect(
                renderer,
                SDL_FRect{iconCenterX - thickness / 2.0f,
                          iconCenterY - thickness / 2.0f, thickness,
                          thickness},
                Colors::white);
        }

        static void drawThickLine(SDL_Renderer *renderer, const float x1,
                                  const float y1, const float x2,
                                  const float y2, const float thickness,
                                  const SDL_Color &color)
        {
            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.0f)
            {
                return;
            }

            const float halfThickness = thickness / 2.0f;
            const float px = -dy / length * halfThickness;
            const float py = dx / length * halfThickness;
            const SDL_FColor fcolor{color.r / 255.0f, color.g / 255.0f,
                                    color.b / 255.0f, color.a / 255.0f};

            SDL_Vertex vertices[4]{};
            vertices[0].position = SDL_FPoint{x1 + px, y1 + py};
            vertices[1].position = SDL_FPoint{x1 - px, y1 - py};
            vertices[2].position = SDL_FPoint{x2 - px, y2 - py};
            vertices[3].position = SDL_FPoint{x2 + px, y2 + py};
            for (auto &vertex : vertices)
            {
                vertex.color = fcolor;
                vertex.tex_coord = SDL_FPoint{0.0f, 0.0f};
            }

            constexpr int indices[6]{0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
        }
    };

    class TabStrip : public Component
    {
    public:
        explicit TabStrip(State *stateToUse) : Component(stateToUse, "TabStrip")
        {
            rebuildButtons();
            lastTabCount = state ? static_cast<int>(state->tabs.size()) : 0;
            lastActiveTabIndex = state ? state->activeTabIndex : -1;
            if (state)
            {
                lastTitles.reserve(state->tabs.size());
                lastTooltipTexts.reserve(state->tabs.size());
                for (const auto &tab : state->tabs)
                {
                    lastTitles.push_back(
                        cupuacu::actions::documentTabTitle(tab));
                    lastTooltipTexts.push_back(tab.session.currentFile);
                }
            }
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
            Helpers::fillRect(
                renderer,
                SDL_Rect{0, std::max(0, getHeight() - 1), getWidth(), 1},
                Colors::border);
        }

        void resized() override
        {
            layoutButtons();
        }

        void timerCallback() override
        {
            const int tabCount =
                state ? static_cast<int>(state->tabs.size()) : 0;
            const int activeIndex = state ? state->activeTabIndex : -1;
            std::vector<std::string> titles;
            std::vector<std::string> tooltipTexts;
            titles.reserve(tabCount);
            tooltipTexts.reserve(tabCount);
            for (const auto &tab : state->tabs)
            {
                titles.push_back(cupuacu::actions::documentTabTitle(tab));
                tooltipTexts.push_back(tab.session.currentFile);
            }

            if (tabCount != lastTabCount)
            {
                lastTabCount = tabCount;
                lastTitles = std::move(titles);
                lastTooltipTexts = std::move(tooltipTexts);
                rebuildButtons();
                layoutButtons();
                setDirty();
            }
            else if (titles != lastTitles || tooltipTexts != lastTooltipTexts)
            {
                lastTitles = titles;
                lastTooltipTexts = tooltipTexts;
                updateTabMetadata();
                setDirty();
            }

            if (activeIndex != lastActiveTabIndex)
            {
                lastActiveTabIndex = activeIndex;
                updateButtonState();
            }

            const bool switchingAllowed =
                !state || !state->audioDevices ||
                (!state->audioDevices->isPlaying() &&
                 !state->audioDevices->isRecording());
            if (switchingAllowed != lastSwitchingAllowed)
            {
                lastSwitchingAllowed = switchingAllowed;
                updateButtonState();
            }
        }

    private:
        std::vector<TabStripTab *> tabs;
        int lastTabCount = -1;
        int lastActiveTabIndex = -1;
        bool lastSwitchingAllowed = true;
        std::vector<std::string> lastTitles;
        std::vector<std::string> lastTooltipTexts;

        void rebuildButtons()
        {
            removeAllChildren();
            tabs.clear();

            if (!state)
            {
                return;
            }

            for (int i = 0; i < static_cast<int>(state->tabs.size()); ++i)
            {
                auto *tab = emplaceChild<TabStripTab>(
                    state, cupuacu::actions::documentTabTitle(state->tabs[i]),
                    i);
                tab->setTooltipText(state->tabs[i].session.currentFile);
                tab->setOnSelect(
                    [this](const int index)
                    {
                        cupuacu::actions::switchToTab(state, index);
                    });
                tab->setOnClose(
                    [this](const int index)
                    {
                        cupuacu::actions::closeTab(state, index);
                    });
                tabs.push_back(tab);
            }

            updateButtonState();
        }

        void updateButtonState()
        {
            const bool switchingAllowed =
                !state || !state->audioDevices ||
                (!state->audioDevices->isPlaying() &&
                 !state->audioDevices->isRecording());

            for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
            {
                auto *tab = tabs[i];
                if (!tab)
                {
                    continue;
                }

                tab->setActive(i == state->activeTabIndex);
                tab->setEnabledState(switchingAllowed);
            }
        }

        void updateTabMetadata()
        {
            if (!state)
            {
                return;
            }

            for (int i = 0; i < static_cast<int>(tabs.size()) &&
                            i < static_cast<int>(state->tabs.size());
                 ++i)
            {
                if (auto *tab = tabs[i])
                {
                    tab->setTitle(
                        cupuacu::actions::documentTabTitle(state->tabs[i]));
                    tab->setTooltipText(state->tabs[i].session.currentFile);
                }
            }
        }

        void layoutButtons()
        {
            const int tabCount = static_cast<int>(tabs.size());
            if (tabCount <= 0)
            {
                return;
            }

            const int padding = scaleUi(state, 4.0f);
            const int gap = scaleUi(state, 4.0f);
            const int tabHeight = std::max(1, getHeight() - padding * 2);
            const int maxTabWidth = std::max(
                scaleUi(state, 80.0f),
                std::max(0, getWidth() - padding * 2 - gap * 4) / 5);
            const int fittedWidth = std::max(
                scaleUi(state, 80.0f),
                std::max(0, getWidth() - padding * 2 - gap * (tabCount - 1)) /
                    std::max(1, tabCount));
            const int tabWidth = tabCount > 5 ? fittedWidth : maxTabWidth;

            int x = padding;
            for (auto *tab : tabs)
            {
                if (!tab)
                {
                    continue;
                }
                tab->setBounds(x, padding, tabWidth, tabHeight);
                x += tabWidth + gap;
            }
        }
    };
} // namespace cupuacu::gui
