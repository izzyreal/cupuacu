#pragma once

#include "Component.h"
#include "Label.h"

#include <functional>
#include <string>
#include <vector>

namespace cupuacu::gui
{
    class DropdownMenu : public Component
    {
    private:
        std::vector<std::string> items;
        std::vector<Label *> itemLabels;
        int selectedIndex = -1;
        bool expanded = false;
        int collapsedHeight = 0;
        int fontSizeOverride = 0;
        int itemMargin = 6; // virtual pixels
        int hoveredIndex = -1;
        std::function<void(int)> onSelectionChanged;

        int getFontSize() const;
        int getRowHeight() const;
        void rebuildLabels();
        void updateLabelStyles();
        void updateLabelVisibility();

    public:
        DropdownMenu(cupuacu::State *stateToUse);

        void setItems(const std::vector<std::string> &itemsToUse);
        void setSelectedIndex(const int index);
        void setFontSize(const int fontSize);
        void setItemMargin(const int margin);
        void setOnSelectionChanged(std::function<void(int)> callback);
        void setCollapsedHeight(const int height);
        int getSelectedIndex() const
        {
            return selectedIndex;
        }

        void setExpanded(const bool expandedToUse);

        void resized() override;
        void onDraw(SDL_Renderer *renderer) override;
        bool mouseDown(const MouseEvent &) override;
        bool mouseMove(const MouseEvent &) override;
        void mouseLeave() override;
    };
} // namespace cupuacu::gui
