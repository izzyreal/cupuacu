#include "TextInput.hpp"

#include "Colors.hpp"
#include "Helpers.hpp"
#include "UiScale.hpp"
#include "Window.hpp"
#include "text.hpp"

#include <algorithm>
#include <limits>

namespace
{
    constexpr SDL_Color kInputFill{58, 58, 58, 255};
    constexpr SDL_Color kInputFillFocused{68, 82, 110, 255};
    constexpr SDL_Color kInputBorder{28, 28, 28, 255};
    constexpr SDL_Color kInputBorderFocused{120, 156, 214, 255};
    constexpr SDL_Color kInputSelectionFill{120, 156, 214, 255};
    constexpr SDL_Color kInputCaretColor{255, 255, 255, 255};
    constexpr Uint64 kCursorBlinkIntervalMs = 500;

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
    collapseSelectionTo(text.size());
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
    resetCursorBlink();
    if (window && window->getSdlWindow())
    {
        SDL_StartTextInput(window->getSdlWindow());
    }
    setDirty();
}

void TextInput::focusLost()
{
    focused = false;
    cursorVisible = false;
    mouseSelecting = false;
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

    const std::size_t index = findCursorIndexForX(event.mouseXi);
    collapseSelectionTo(index);
    mouseSelecting = true;
    resetCursorBlink();
    return true;
}

bool TextInput::mouseMove(const MouseEvent &event)
{
    if (!mouseSelecting)
    {
        return false;
    }

    const std::size_t nextIndex = findCursorIndexForX(event.mouseXi);
    if (cursorIndex == nextIndex)
    {
        return true;
    }

    cursorIndex = nextIndex;
    resetCursorBlink();
    setDirty();
    return true;
}

bool TextInput::mouseUp(const MouseEvent &)
{
    if (!mouseSelecting)
    {
        return false;
    }

    mouseSelecting = false;
    return true;
}

bool TextInput::keyDown(const SDL_KeyboardEvent &event)
{
    if (!focused)
    {
        return false;
    }

    const bool extendSelection = (event.mod & SDL_KMOD_SHIFT) != 0;

    if (event.scancode == SDL_SCANCODE_LEFT)
    {
        if (cursorIndex > 0)
        {
            moveCaretTo(cursorIndex - 1, extendSelection);
        }
        else if (!extendSelection && hasSelection())
        {
            collapseSelectionTo(getSelectionStart());
        }
        return true;
    }

    if (event.scancode == SDL_SCANCODE_RIGHT)
    {
        if (cursorIndex < text.size())
        {
            moveCaretTo(cursorIndex + 1, extendSelection);
        }
        else if (!extendSelection && hasSelection())
        {
            collapseSelectionTo(getSelectionEnd());
        }
        return true;
    }

    if (event.scancode == SDL_SCANCODE_BACKSPACE)
    {
        if (hasSelection())
        {
            deleteSelectionIfActive();
            notifyTextChanged();
        }
        else if (cursorIndex > 0)
        {
            text.erase(cursorIndex - 1, 1);
            collapseSelectionTo(cursorIndex - 1);
            notifyTextChanged();
        }
        return true;
    }

    if (event.scancode == SDL_SCANCODE_DELETE)
    {
        if (hasSelection())
        {
            deleteSelectionIfActive();
            notifyTextChanged();
        }
        else if (cursorIndex < text.size())
        {
            text.erase(cursorIndex, 1);
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

    std::string filteredText;
    filteredText.reserve(textToInsert.size());
    for (const char c : textToInsert)
    {
        if (!isCharacterAllowed(c))
        {
            continue;
        }
        filteredText.push_back(c);
    }

    if (!filteredText.empty())
    {
        deleteSelectionIfActive();
        text.insert(cursorIndex, filteredText);
        collapseSelectionTo(cursorIndex + filteredText.size());
        notifyTextChanged();
        return true;
    }

    return false;
}

void TextInput::timerCallback()
{
    if (!focused)
    {
        return;
    }

    const Uint64 now = SDL_GetTicks();
    if (now - lastCursorBlinkTicks < kCursorBlinkIntervalMs)
    {
        return;
    }

    lastCursorBlinkTicks = now;
    cursorVisible = !cursorVisible;
    setDirty();
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

    const int padding = getTextPadding();
    const int contentHeight = std::max(0, bounds.h - borderThickness * 2);
    const int textY = getTextY(bounds, borderThickness);

    if (hasSelection())
    {
        const int selectionX =
            padding + measureTextWidth(std::string_view(text).substr(
                          0, getSelectionStart()));
        const int selectionWidth =
            measureTextWidth(std::string_view(text).substr(
                getSelectionStart(), getSelectionEnd() - getSelectionStart()));
        if (selectionWidth > 0)
        {
            Helpers::fillRect(renderer,
                              SDL_Rect{selectionX, borderThickness,
                                       selectionWidth, contentHeight},
                              kInputSelectionFill);
        }
    }

    const SDL_FRect textBounds{
        static_cast<float>(padding),
        static_cast<float>(textY),
        static_cast<float>(std::max(0, bounds.w - padding * 2)),
        static_cast<float>(contentHeight)};
    renderText(renderer, text, static_cast<uint8_t>(fontSize), textBounds, false);

    if (focused && cursorVisible)
    {
        const int caretX =
            padding +
            measureTextWidth(std::string_view(text).substr(0, cursorIndex));
        const int caretThickness = std::max(1, scaleUi(state, 2.0f));
        const auto [_, textHeight] =
            measureText(text.empty() ? "Ag" : text, static_cast<uint8_t>(fontSize));
        const int caretHeight =
            std::max(1, std::min(contentHeight, static_cast<int>(std::ceil(textHeight))));
        Helpers::fillRect(
            renderer,
            SDL_Rect{caretX, textY, caretThickness, caretHeight},
            kInputCaretColor);
    }
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
    resetCursorBlink();
    setDirty();
    if (onTextChanged)
    {
        onTextChanged(text);
    }
}

bool TextInput::hasSelection() const
{
    return cursorIndex != selectionAnchorIndex;
}

std::size_t TextInput::getSelectionStart() const
{
    return std::min(cursorIndex, selectionAnchorIndex);
}

std::size_t TextInput::getSelectionEnd() const
{
    return std::max(cursorIndex, selectionAnchorIndex);
}

void TextInput::collapseSelectionTo(const std::size_t index)
{
    const std::size_t clamped = std::min(index, text.size());
    cursorIndex = clamped;
    selectionAnchorIndex = clamped;
    setDirty();
}

void TextInput::moveCaretTo(const std::size_t index, const bool extendSelection)
{
    cursorIndex = std::min(index, text.size());
    if (!extendSelection)
    {
        selectionAnchorIndex = cursorIndex;
    }
    resetCursorBlink();
    setDirty();
}

void TextInput::resetCursorBlink()
{
    cursorVisible = focused;
    lastCursorBlinkTicks = SDL_GetTicks();
}

void TextInput::deleteSelectionIfActive()
{
    if (!hasSelection())
    {
        return;
    }

    const std::size_t start = getSelectionStart();
    text.erase(start, getSelectionEnd() - start);
    collapseSelectionTo(start);
}

int TextInput::getTextPadding() const
{
    return scaleUi(state, 8.0f);
}

int TextInput::getTextY(const SDL_Rect &bounds, const int borderThickness) const
{
    const auto [_, textHeight] =
        measureText(text.empty() ? "Ag" : text, static_cast<uint8_t>(fontSize));
    const int contentHeight = std::max(0, bounds.h - borderThickness * 2);
    return borderThickness +
           std::max(0, (contentHeight - static_cast<int>(std::ceil(textHeight))) / 2);
}

int TextInput::measureTextWidth(const std::string_view value) const
{
    return measureText(std::string(value), static_cast<uint8_t>(fontSize)).first;
}

std::size_t TextInput::findCursorIndexForX(const int localX) const
{
    const int relativeX = std::max(0, localX - getTextPadding());
    std::size_t bestIndex = 0;
    int bestDistance = std::numeric_limits<int>::max();

    for (std::size_t index = 0; index <= text.size(); ++index)
    {
        const int candidateX =
            measureTextWidth(std::string_view(text).substr(0, index));
        const int distance = std::abs(candidateX - relativeX);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    return bestIndex;
}
