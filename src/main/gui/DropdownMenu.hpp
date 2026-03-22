#pragma once

#include "Component.hpp"
#include "Label.hpp"

#include <functional>
#include <string>
#include <vector>

namespace cupuacu::gui
{
    class DropdownMenu;
    class Window;

    class DropdownOwnerComponent
    {
    public:
        virtual ~DropdownOwnerComponent() = default;
        virtual DropdownMenu *getOwningDropdown() const = 0;
    };

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
        std::unique_ptr<Window> popupWindow;
        Component *previousFocusedComponent = nullptr;
        std::function<void(int)> onSelectionChanged;

        int getFontSize() const;
        void rebuildLabels();
        void updateLabelStyles() const;
        void updateLabelVisibility() const;
        void destroyPopupWindow();
        void ensurePopupWindow();
        void handlePopupSelection(int index);

    public:
        ~DropdownMenu() override;
        DropdownMenu(State *stateToUse);

        void setItems(const std::vector<std::string> &itemsToUse);
        void setSelectedIndex(const int index);
        void setFontSize(const int fontSize);
        void setItemMargin(const int margin);
        void setOnSelectionChanged(std::function<void(int)> callback);
        void setCollapsedHeight(const int height);
        int getRowHeight() const;
        int getPopupRowHeight() const;
        int getSelectedIndex() const
        {
            return selectedIndex;
        }
        bool isExpanded() const
        {
            return expanded;
        }
        bool acceptsKeyboardFocus() const override
        {
            return true;
        }
        bool shouldCaptureMouse() const override
        {
            return false;
        }

        void setExpanded(const bool expandedToUse);

        void resized() override;
        void onDraw(SDL_Renderer *renderer) override;
        bool mouseDown(const MouseEvent &) override;
        bool mouseMove(const MouseEvent &) override;
        void mouseLeave() override;
    };
} // namespace cupuacu::gui
