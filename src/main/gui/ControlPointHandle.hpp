#pragma once

#include "Component.hpp"

namespace cupuacu::gui
{
    class ControlPointHandle : public Component
    {
    public:
        ControlPointHandle(State *stateToUse, std::string componentNameToUse)
            : Component(stateToUse, std::move(componentNameToUse))
        {
        }

        void setFillColors(const SDL_Color idleColorToUse,
                           const SDL_Color activeColorToUse)
        {
            if (idleColor.r == idleColorToUse.r &&
                idleColor.g == idleColorToUse.g &&
                idleColor.b == idleColorToUse.b &&
                idleColor.a == idleColorToUse.a &&
                activeColor.r == activeColorToUse.r &&
                activeColor.g == activeColorToUse.g &&
                activeColor.b == activeColorToUse.b &&
                activeColor.a == activeColorToUse.a)
            {
                return;
            }

            idleColor = idleColorToUse;
            activeColor = activeColorToUse;
            setDirty();
        }

        void setActive(const bool shouldBeActive)
        {
            if (active != shouldBeActive)
            {
                active = shouldBeActive;
                setDirty();
            }
        }

        void mouseEnter() override
        {
            setDirty();
        }

        void mouseLeave() override
        {
            setDirty();
        }

        void onDraw(SDL_Renderer *renderer) override;

    private:
        bool active = false;
        SDL_Color idleColor{0, 185, 0, 255};
        SDL_Color activeColor{0, 255, 0, 255};
    };
} // namespace cupuacu::gui
