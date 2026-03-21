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
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cupuacu::gui
{
    namespace
    {
        constexpr SDL_Color kTabDisabledFill{56, 56, 56, 255};
        constexpr SDL_Color kTabBaseFill{78, 78, 78, 255};
        constexpr SDL_Color kTabHoverFill{96, 96, 96, 255};
        constexpr SDL_Color kTabPressedFill{66, 92, 134, 255};
        constexpr SDL_Color kTabActiveFill{74, 110, 170, 255};
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

            closeLabel = emplaceChild<Label>(state, "x");
            closeLabel->setInterceptMouseEnabled(false);
            closeLabel->setCenterHorizontally(true);
            closeLabel->setFontSize(state->menuFontSize - 6);
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
            const Uint8 opacity = enabled ? 255 : 160;
            titleLabel->setOpacity(opacity);
            closeLabel->setOpacity(opacity);
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

        void resized() override
        {
            const SDL_Rect closeRect = getCloseBounds();
            const int titlePadding = scaleUi(state, 12.0f);
            titleLabel->setBounds(
                titlePadding, 0,
                std::max(0, closeRect.x - titlePadding * 2), getHeight());
            closeLabel->setBounds(closeRect);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const SDL_FRect bounds = getLocalBoundsF();
            const float radius = scaleUiF(state, 8.0f);
            SDL_Color fill = kTabBaseFill;

            if (!enabled)
            {
                fill = kTabDisabledFill;
            }
            else if (pressTarget != PressTarget::None &&
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

            drawRoundedRect(renderer, bounds, radius, fill);
            drawRoundedRectOutline(renderer, bounds, radius, Colors::border);
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

            pressTarget = isInsideRect(getCloseBounds(), e.mouseXi, e.mouseYi)
                              ? PressTarget::Close
                              : PressTarget::Body;
            pointerInsideWhilePressed = true;
            setDirty();
            return true;
        }

        bool mouseMove(const MouseEvent &e) override
        {
            if (pressTarget == PressTarget::None)
            {
                return false;
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
        bool pointerInsideWhilePressed = false;
        PressTarget pressTarget = PressTarget::None;
        std::string title;
        Label *titleLabel = nullptr;
        Label *closeLabel = nullptr;
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
            const int closeWidth = scaleUi(state, 28.0f);
            const int horizontalPadding = scaleUi(state, 6.0f);
            return {
                std::max(horizontalPadding,
                         getWidth() - closeWidth - horizontalPadding),
                0,
                closeWidth,
                getHeight()};
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
                for (const auto &tab : state->tabs)
                {
                    lastTitles.push_back(
                        cupuacu::actions::documentTabTitle(tab));
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
            titles.reserve(tabCount);
            for (const auto &tab : state->tabs)
            {
                titles.push_back(cupuacu::actions::documentTabTitle(tab));
            }

            if (tabCount != lastTabCount)
            {
                lastTabCount = tabCount;
                lastTitles = std::move(titles);
                rebuildButtons();
                layoutButtons();
                setDirty();
            }
            else if (titles != lastTitles)
            {
                lastTitles = titles;
                updateTitles();
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

        void updateTitles()
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
