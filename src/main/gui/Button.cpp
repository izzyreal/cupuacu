#include "Button.hpp"

#include "Colors.hpp"
#include "Helpers.hpp"

#include <utility>

namespace
{
    constexpr SDL_Color kDisabledColor{56, 56, 56, 255};
    constexpr SDL_Color kBaseColor{78, 78, 78, 255};
    constexpr SDL_Color kHoverColor{96, 96, 96, 255};
    constexpr SDL_Color kPressedColor{66, 92, 134, 255};
    constexpr SDL_Color kToggledColor{74, 110, 170, 255};
}

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
    else if (type == ButtonType::Toggle && toggled)
    {
        fillColor = kToggledColor;
    }
    else if (isMouseOver())
    {
        fillColor = kHoverColor;
    }

    Helpers::fillRect(renderer, bounds, fillColor);

    const SDL_FRect borderRect{0.0f, 0.0f, static_cast<float>(bounds.w),
                               static_cast<float>(bounds.h)};
    Helpers::setRenderDrawColor(renderer, Colors::border);
    SDL_RenderRect(renderer, &borderRect);
}
