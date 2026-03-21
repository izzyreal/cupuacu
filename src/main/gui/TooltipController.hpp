#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace cupuacu
{
    struct State;
}

namespace cupuacu::gui
{
    class Component;
    class Window;
    class TooltipPopupWindow;

    class TooltipController
    {
    public:
        TooltipController(State *stateToUse, Window *windowToUse);
        ~TooltipController();

        void update();
        void hide();

    private:
        State *state = nullptr;
        Window *window = nullptr;
        std::unique_ptr<TooltipPopupWindow> popupWindow;
        Component *hoveredSource = nullptr;
        std::string hoveredText;
        SDL_Rect hoveredAnchor{};
        Uint64 hoveredSinceTicks = 0;
        Component *shownSource = nullptr;
        std::string shownText;
        SDL_Rect shownAnchor{};

        Component *resolveTooltipSource(Component *component) const;
    };
} // namespace cupuacu::gui
