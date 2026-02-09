#include "Button.hpp"

#include "../State.hpp"
#include "Colors.hpp"
#include "Helpers.hpp"

#include <utility>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr SDL_Color kDisabledColor{56, 56, 56, 255};
    constexpr SDL_Color kBaseColor{78, 78, 78, 255};
    constexpr SDL_Color kHoverColor{96, 96, 96, 255};
    constexpr SDL_Color kPressedColor{66, 92, 134, 255};
    constexpr SDL_Color kToggledColor{74, 110, 170, 255};

    int computeButtonBorderThicknessForScale(const uint8_t pixelScale)
    {
        const int safeScale = std::max(1, static_cast<int>(pixelScale));
        return std::max(1, static_cast<int>(std::lround(4.0 / safeScale)));
    }
} // namespace

using namespace cupuacu::gui;

Button::Button(State *state, const std::string &componentName,
               const ButtonType typeToUse)
    : Component(state, componentName), type(typeToUse)
{
}

void Button::setOnPress(std::function<void()> onPressToUse)
{
    onPress = std::move(onPressToUse);
}

void Button::setOnToggle(std::function<void(bool)> onToggleToUse)
{
    onToggle = std::move(onToggleToUse);
}

void Button::setEnabled(const bool enabledToUse)
{
    if (enabled == enabledToUse)
    {
        return;
    }

    enabled = enabledToUse;
    pressed = false;
    pointerInsideWhilePressed = false;
    setDirty();
}

void Button::setToggled(const bool toggledToUse)
{
    if (toggled == toggledToUse)
    {
        return;
    }
    toggled = toggledToUse;
    setDirty();
}

void Button::setForcedFillColor(const std::optional<SDL_Color> &color)
{
    const bool bothEmpty = !forcedFillColor.has_value() && !color.has_value();
    const bool bothEqual =
        forcedFillColor.has_value() && color.has_value() &&
        forcedFillColor->r == color->r && forcedFillColor->g == color->g &&
        forcedFillColor->b == color->b && forcedFillColor->a == color->a;
    if (bothEmpty || bothEqual)
    {
        return;
    }
    forcedFillColor = color;
    setDirty();
}

bool Button::isInside(const MouseEvent &e) const
{
    return e.mouseXi >= 0 && e.mouseYi >= 0 && e.mouseXi < getWidth() &&
           e.mouseYi < getHeight();
}

bool Button::mouseDown(const MouseEvent &e)
{
    if (!enabled)
    {
        return false;
    }

    const bool inside = isInside(e);
    pressed = true;
    pointerInsideWhilePressed = inside;

    if (inside)
    {
        if (type == ButtonType::Toggle)
        {
            toggled = !toggled;
            if (onToggle)
            {
                onToggle(toggled);
            }
        }

        if (onPress)
        {
            onPress();
        }
    }

    setDirty();
    return true;
}

bool Button::mouseUp(const MouseEvent &e)
{
    if (!enabled)
    {
        return false;
    }

    pressed = false;
    pointerInsideWhilePressed = false;

    setDirty();
    return true;
}

bool Button::mouseMove(const MouseEvent &e)
{
    if (!enabled || !pressed)
    {
        return false;
    }

    const bool inside = isInside(e);
    if (inside != pointerInsideWhilePressed)
    {
        pointerInsideWhilePressed = inside;
        setDirty();
    }
    return true;
}

void Button::mouseEnter()
{
    setDirty();
}

void Button::mouseLeave()
{
    setDirty();
}

void Button::onDraw(SDL_Renderer *renderer)
{
    const SDL_Rect bounds = getLocalBounds();

    SDL_Color fillColor = kBaseColor;
    if (!enabled)
    {
        fillColor = kDisabledColor;
    }
    else if (pressed && pointerInsideWhilePressed)
    {
        fillColor = kPressedColor;
    }
    else if (forcedFillColor.has_value())
    {
        fillColor = *forcedFillColor;
    }
    else if (type == ButtonType::Toggle && toggled)
    {
        fillColor = kToggledColor;
    }
    else if (isMouseOver())
    {
        fillColor = kHoverColor;
    }

    Helpers::fillRect(renderer, bounds, fillColor);

    const int borderThickness = std::min(
        computeButtonBorderThicknessForScale(state->pixelScale),
        std::max(1, std::min(bounds.w, bounds.h) / 2));

    Helpers::fillRect(renderer, SDL_Rect{0, 0, bounds.w, borderThickness},
                      Colors::border);
    Helpers::fillRect(renderer,
                      SDL_Rect{0, bounds.h - borderThickness, bounds.w,
                               borderThickness},
                      Colors::border);
    Helpers::fillRect(renderer,
                      SDL_Rect{0, borderThickness, borderThickness,
                               bounds.h - borderThickness * 2},
                      Colors::border);
    Helpers::fillRect(renderer,
                      SDL_Rect{bounds.w - borderThickness, borderThickness,
                               borderThickness, bounds.h - borderThickness * 2},
                      Colors::border);
}
