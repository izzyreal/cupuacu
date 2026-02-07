#pragma once

#include "Button.hpp"
#include "Label.hpp"

namespace cupuacu::gui
{
    class TextButton : public Button
    {
    public:
        TextButton(State *state, const std::string &textToUse,
                   const ButtonType typeToUse = ButtonType::Momentary);

        void setText(const std::string &textToUse);
        void resized() override;

    private:
        Label *label;
    };
} // namespace cupuacu::gui
