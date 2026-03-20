#pragma once

#include <algorithm>
#include <vector>

namespace cupuacu::gui
{
    struct MenuSubMenuLayoutPlanItem
    {
        bool visible = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    inline std::vector<MenuSubMenuLayoutPlanItem> planMenuSubMenuLayout(
        const bool firstLevel, const int parentWidth, const int parentHeight,
        const int menuItemHeight, const int nestedHorizontalOverlap,
        const int horizontalMargin, const std::vector<int> &textWidths,
        const std::vector<bool> &shouldShow)
    {
        std::vector<MenuSubMenuLayoutPlanItem> plan(textWidths.size());
        if (textWidths.size() != shouldShow.size())
        {
            return plan;
        }

        int maxTextWidth = 1;
        for (std::size_t i = 0; i < textWidths.size(); ++i)
        {
            if (!shouldShow[i])
            {
                continue;
            }
            maxTextWidth = std::max(maxTextWidth, textWidths[i]);
        }

        const int xPos = firstLevel ? 0 : parentWidth - nestedHorizontalOverlap;
        int yPos = firstLevel ? parentHeight : 0;
        for (std::size_t i = 0; i < plan.size(); ++i)
        {
            if (!shouldShow[i])
            {
                continue;
            }

            plan[i] = {
                .visible = true,
                .x = xPos,
                .y = yPos,
                .width = maxTextWidth + horizontalMargin,
                .height = menuItemHeight,
            };
            yPos += menuItemHeight;
        }

        return plan;
    }
} // namespace cupuacu::gui
