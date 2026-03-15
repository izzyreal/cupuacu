#include "TextInput.hpp"

#include "Colors.hpp"
#include "Helpers.hpp"
#include "UiScale.hpp"
#include "Window.hpp"
#include "text.hpp"

#include <algorithm>

namespace
{
    constexpr SDL_Color kInputFill{58, 58, 58, 255};
    constexpr SDL_Color kInputFillFocused{68, 82, 110, 255};
    constexpr SDL_Color kInputBorder{28, 28, 28, 255};
    constexpr SDL_Color kInputBorderFocused{120, 156, 214, 255};

    int computeInputBorderThicknessForScale(const cupuacu::State *state)
    {
        return cupuacu::gui::scaleUi(state, 3.0f);
    }
} // namespace

using namespace cupuacu::gui;

TextInput::TextInput(State *stateToUse)
    : Component(stateToUse, "TextInput")
{
    fontSize = state ? std::max(1, static_cast<int>(state->menuFontSize) - 6) : 18;
}

void TextInput::setText(const std::string &textToUse)
{
    if (text == textToUse)
    {
        return;
    }

    text = textToUse;
    setDirty();
}

void TextInput::setFontSize(const int pointSizeToUse)
{
    if (fontSize == pointSizeToUse)
    {
        return;
    }
    fontSize = std::max(1, pointSizeToUse);
    setDirty();
}

void TextInput::setAllowedCharacters(const std::string &charactersToUse)
{
    allowedCharacters = charactersToUse;
}

void TextInput::setOnTextChanged(
    std::function<void(const std::string &)> callback)
{
    onTextChanged = std::move(callback);
}

void TextInput::setOnEditingFinished(
    std::function<void(const std::string &)> callback)
{
    onEditingFinished = std::move(callback);
}

void TextInput::focusGained()
{
    focused = true;
    if (window && window->getSdlWindow())
    {
        SDL_StartTextInput(window->getSdlWindow());
    }
    setDirty();
}

void TextInput::focusLost()
{
    focused = false;
    if (window && window->getSdlWindow())
    {
        SDL_StopTextInput(window->getSdlWindow());
    }
    if (onEditingFinished)
    {
        onEditingFinished(text);
    }
    setDirty();
}

bool TextInput::mouseDown(const MouseEvent &event)
{
    if (event.mouseXi < 0 || event.mouseYi < 0 || event.mouseXi >= getWidth() ||
        event.mouseYi >= getHeight())
    {
        return false;
    }

    if (window)
    {
        window->setFocusedComponent(this);
    }
    return true;
}

bool TextInput::keyDown(const SDL_KeyboardEvent &event)
{
    if (!focused)
    {
        return false;
    }

    if (event.scancode == SDL_SCANCODE_BACKSPACE)
    {
        if (!text.empty())
        {
            text.pop_back();
            notifyTextChanged();
        }
        return true;
    }

    if (event.scancode == SDL_SCANCODE_RETURN ||
        event.scancode == SDL_SCANCODE_RETURN2 ||
        event.scancode == SDL_SCANCODE_KP_ENTER)
    {
        if (onEditingFinished)
        {
            onEditingFinished(text);
        }
        return true;
    }

    if (event.scancode == SDL_SCANCODE_ESCAPE)
    {
        if (window)
        {
            window->setFocusedComponent(nullptr);
        }
        return true;
    }

    return false;
}

bool TextInput::textInput(const std::string_view textToInsert)
{
    if (!focused || textToInsert.empty())
    {
        return false;
    }

    bool changed = false;
    for (const char c : textToInsert)
    {
        if (!isCharacterAllowed(c))
        {
            continue;
        }
        text.push_back(c);
        changed = true;
    }

    if (changed)
    {
        notifyTextChanged();
    }

    return changed;
}

void TextInput::onDraw(SDL_Renderer *renderer)
{
    const SDL_Rect bounds = getLocalBounds();
    Helpers::fillRect(renderer, bounds, focused ? kInputFillFocused : kInputFill);

    const int borderThickness = std::min(
        computeInputBorderThicknessForScale(state),
        std::max(1, std::min(bounds.w, bounds.h) / 2));
    const SDL_Color borderColor = focused ? kInputBorderFocused : kInputBorder;

    Helpers::fillRect(renderer, SDL_Rect{0, 0, bounds.w, borderThickness},
                      borderColor);
    Helpers::fillRect(renderer,
                      SDL_Rect{0, bounds.h - borderThickness, bounds.w,
                               borderThickness},
                      borderColor);
    Helpers::fillRect(renderer,
                      SDL_Rect{0, borderThickness, borderThickness,
                               bounds.h - borderThickness * 2},
                      borderColor);
    Helpers::fillRect(renderer,
                      SDL_Rect{bounds.w - borderThickness, borderThickness,
                               borderThickness, bounds.h - borderThickness * 2},
                      borderColor);

    std::string displayText = text;
    if (focused)
    {
        displayText += "|";
    }

    const auto [_, textHeight] =
        measureText(displayText.empty() ? "Ag" : displayText,
                    static_cast<uint8_t>(fontSize));
    const int padding = scaleUi(state, 8.0f);
    const int contentHeight = std::max(0, bounds.h - borderThickness * 2);
    const int textY =
        borderThickness +
        std::max(0, (contentHeight - static_cast<int>(std::ceil(textHeight))) / 2);
    const SDL_FRect textBounds{
        static_cast<float>(padding),
        static_cast<float>(textY),
        static_cast<float>(std::max(0, bounds.w - padding * 2)),
        static_cast<float>(contentHeight)};
    renderText(renderer, displayText, static_cast<uint8_t>(fontSize), textBounds,
               false);
}

bool TextInput::isCharacterAllowed(const char c) const
{
    if (allowedCharacters.empty())
    {
        return true;
    }
    return allowedCharacters.find(c) != std::string::npos;
}

void TextInput::notifyTextChanged()
{
    setDirty();
    if (onTextChanged)
    {
        onTextChanged(text);
    }
}
