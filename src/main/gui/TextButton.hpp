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
        void setFontSize(int fontSize);
        void setLabelCenterHorizontally(bool shouldCenter);
        void setLabelMargin(int margin);
        void setLabelOverflowMode(TextOverflowMode overflowMode);
        void setTooltipTextForTruncatedLabel(const std::string &tooltipText);
        std::string getTooltipText() const override;
        void resized() override;

    private:
        Label *label;
        std::string truncatedLabelTooltipText;
    };
} // namespace cupuacu::gui
