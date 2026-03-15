#pragma once

#include "Component.hpp"

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
        bool keyDown(const SDL_KeyboardEvent &event) override;
        bool textInput(const std::string_view textToInsert) override;
        void onDraw(SDL_Renderer *renderer) override;

    private:
        std::string text;
        std::string allowedCharacters;
        int fontSize = 0;
        bool focused = false;
        std::function<void(const std::string &)> onTextChanged;
        std::function<void(const std::string &)> onEditingFinished;

        bool isCharacterAllowed(char c) const;
        void notifyTextChanged();
    };
} // namespace cupuacu::gui
