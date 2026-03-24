#pragma once

#include "Component.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace cupuacu::gui
{
    class TextInput : public Component
    {
    public:
        explicit TextInput(State *stateToUse);

        void setText(const std::string &textToUse);
        const std::string &getText() const
        {
            return text;
        }

        void setFontSize(int pointSizeToUse);
        void setAllowedCharacters(const std::string &charactersToUse);
        void setOnTextChanged(std::function<void(const std::string &)> callback);
        void setOnEditingFinished(
            std::function<void(const std::string &)> callback);

        bool acceptsKeyboardFocus() const override
        {
            return true;
        }
        void focusGained() override;
        void focusLost() override;
        bool mouseDown(const MouseEvent &event) override;
        bool mouseMove(const MouseEvent &event) override;
        bool mouseUp(const MouseEvent &event) override;
        bool keyDown(const SDL_KeyboardEvent &event) override;
        bool textInput(const std::string_view textToInsert) override;
        void timerCallback() override;
        void onDraw(SDL_Renderer *renderer) override;

    private:
        std::string text;
        std::string allowedCharacters;
        int fontSize = 0;
        bool focused = false;
        bool cursorVisible = false;
        bool mouseSelecting = false;
        std::size_t cursorIndex = 0;
        std::size_t selectionAnchorIndex = 0;
        Uint64 lastCursorBlinkTicks = 0;
        std::function<void(const std::string &)> onTextChanged;
        std::function<void(const std::string &)> onEditingFinished;

        bool isCharacterAllowed(char c) const;
        void notifyTextChanged();
        bool hasSelection() const;
        std::size_t getSelectionStart() const;
        std::size_t getSelectionEnd() const;
        void collapseSelectionTo(std::size_t index);
        void moveCaretTo(std::size_t index, bool extendSelection);
        void resetCursorBlink();
        void deleteSelectionIfActive();
        uint8_t getEffectiveFontSize() const;
        int getTextPadding() const;
        int getTextY(const SDL_Rect &bounds, int borderThickness) const;
        int measureTextWidth(std::string_view value) const;
        std::size_t findCursorIndexForX(int localX) const;
    };
} // namespace cupuacu::gui
