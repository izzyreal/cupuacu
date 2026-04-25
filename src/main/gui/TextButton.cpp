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

void TextButton::setFontSize(const int fontSize)
{
    label->setFontSize(fontSize);
}

void TextButton::setLabelCenterHorizontally(const bool shouldCenter)
{
    label->setCenterHorizontally(shouldCenter);
}

void TextButton::setLabelMargin(const int margin)
{
    label->setMargin(margin);
}

void TextButton::setLabelOverflowMode(const TextOverflowMode overflowMode)
{
    label->setOverflowMode(overflowMode);
}

void TextButton::setTooltipTextForTruncatedLabel(const std::string &tooltipText)
{
    truncatedLabelTooltipText = tooltipText;
}

std::string TextButton::getTooltipText() const
{
    if (!truncatedLabelTooltipText.empty() && label->isTextTruncated())
    {
        return truncatedLabelTooltipText;
    }
    return "";
}

void TextButton::resized()
{
    label->setBounds(getLocalBounds());
}
