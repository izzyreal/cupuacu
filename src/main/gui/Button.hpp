#pragma once

#include "Component.hpp"

#include <functional>
#include <optional>

namespace cupuacu::gui
{
    enum class ButtonType
    {
        Momentary,
        Toggle
    };

    class Button : public Component
    {
    public:
        Button(State *state, const std::string &componentName,
               const ButtonType typeToUse);

        void setOnPress(std::function<void()> onPressToUse);
        void setOnToggle(std::function<void(bool)> onToggleToUse);
        void setEnabled(bool enabledToUse);
        bool getEnabled() const
        {
            return enabled;
        }
        bool isToggled() const
        {
            return toggled;
        }
        void setToggled(bool toggledToUse);
        void setForcedFillColor(const std::optional<SDL_Color> &color);

        bool mouseDown(const MouseEvent &e) override;
        bool mouseUp(const MouseEvent &e) override;
        bool mouseMove(const MouseEvent &e) override;
        void mouseEnter() override;
        void mouseLeave() override;
        void onDraw(SDL_Renderer *renderer) override;

    private:
        bool isInside(const MouseEvent &e) const;

        ButtonType type;
        bool enabled = true;
        bool pressed = false;
        bool pointerInsideWhilePressed = false;
        bool toggled = false;
        std::optional<SDL_Color> forcedFillColor;
        std::function<void()> onPress;
        std::function<void(bool)> onToggle;
    };
} // namespace cupuacu::gui
