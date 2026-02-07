#include "TextButton.hpp"

using namespace cupuacu::gui;

TextButton::TextButton(State *state, const std::string &textToUse,
                       const ButtonType typeToUse)
    : Button(state, "TextButton:" + textToUse, typeToUse)
{
    label = emplaceChild<Label>(state, textToUse);
    label->setInterceptMouseEnabled(false);
    label->setCenterHorizontally(true);
    label->setFontSize(state->menuFontSize - 6);
}

void TextButton::setText(const std::string &textToUse)
{
    label->setText(textToUse);
}

void TextButton::resized()
{
    label->setBounds(getLocalBounds());
}
