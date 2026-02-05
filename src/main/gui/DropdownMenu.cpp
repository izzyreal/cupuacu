#include "DropdownMenu.h"

#include "Colors.h"
#include "Helpers.h"
#include "text.h"

#include <algorithm>
#include <utility>
#include <cmath>

using namespace cupuacu::gui;

DropdownMenu::DropdownMenu(cupuacu::State *stateToUse)
    : Component(stateToUse, "DropdownMenu")
{
}

int DropdownMenu::getFontSize() const
{
    if (fontSizeOverride > 0)
    {
        return fontSizeOverride;
    }
    const int base = static_cast<int>(state->menuFontSize);
    return std::max(12, base / 2);
}

int DropdownMenu::getRowHeight() const
{
    const int fontSizeVirtual =
        std::max(1, getFontSize() / state->pixelScale);
    const auto [textW, textH] = measureText("Ag", fontSizeVirtual);
    const int textHeight = std::max(1, (int)std::ceil(textH));
    return textHeight + itemMargin * 2;
}

void DropdownMenu::rebuildLabels()
{
    removeChildrenOfType<Label>();
    itemLabels.clear();

    const int fontSize = getFontSize();
    for (const auto &item : items)
    {
        auto label = emplaceChild<Label>(state, item);
        label->setFontSize(fontSize);
        label->setMargin(itemMargin * state->pixelScale);
        label->setCenterHorizontally(false);
        itemLabels.push_back(label);
    }
}

void DropdownMenu::updateLabelStyles()
{
    for (size_t i = 0; i < itemLabels.size(); ++i)
    {
        const bool isSelected = static_cast<int>(i) == selectedIndex;
        itemLabels[i]->setOpacity(isSelected ? 255 : 160);
    }
}

void DropdownMenu::updateLabelVisibility()
{
    for (size_t i = 0; i < itemLabels.size(); ++i)
    {
        const bool shouldShow =
            expanded || static_cast<int>(i) == selectedIndex;
        itemLabels[i]->setVisible(shouldShow);
    }
}

void DropdownMenu::setItems(const std::vector<std::string> &itemsToUse)
{
    items = itemsToUse;
    rebuildLabels();

    if (items.empty())
    {
        selectedIndex = -1;
    }
    else if (selectedIndex < 0 || selectedIndex >= (int)items.size())
    {
        selectedIndex = 0;
    }

    updateLabelStyles();
    updateLabelVisibility();
    resized();
    setDirty();
}

void DropdownMenu::setSelectedIndex(const int index)
{
    if (items.empty())
    {
        selectedIndex = -1;
    }
    else
    {
        selectedIndex = std::clamp(index, 0, (int)items.size() - 1);
    }
    updateLabelStyles();
    updateLabelVisibility();
    resized();
    setDirty();
}

void DropdownMenu::setFontSize(const int fontSize)
{
    fontSizeOverride = fontSize;
    for (auto *label : itemLabels)
    {
        label->setFontSize(getFontSize());
    }
    resized();
    setDirty();
}

void DropdownMenu::setItemMargin(const int margin)
{
    itemMargin = margin;
    for (auto *label : itemLabels)
    {
        label->setMargin(itemMargin * state->pixelScale);
    }
    resized();
    setDirty();
}

void DropdownMenu::setOnSelectionChanged(std::function<void(int)> callback)
{
    onSelectionChanged = std::move(callback);
}

void DropdownMenu::setCollapsedHeight(const int height)
{
    collapsedHeight = height;
    if (!expanded && collapsedHeight > 0)
    {
        setSize(getWidth(), collapsedHeight);
    }
}

void DropdownMenu::setExpanded(const bool expandedToUse)
{
    if (expanded == expandedToUse)
    {
        return;
    }
    expanded = expandedToUse;
    updateLabelVisibility();
    if (expanded)
    {
        bringToFront();
    }
    const int rowHeight = getRowHeight();
    const int expandedHeight =
        std::max(1, static_cast<int>(items.size())) * rowHeight;
    const int targetHeight =
        expanded ? expandedHeight
                 : (collapsedHeight > 0 ? collapsedHeight : rowHeight);
    setSize(getWidth(), targetHeight);
    setDirty();
}

void DropdownMenu::resized()
{
    const int rowHeight = getRowHeight();
    if (!expanded && selectedIndex >= 0 &&
        selectedIndex < (int)itemLabels.size())
    {
        itemLabels[selectedIndex]->setBounds(0, 0, getWidth(), rowHeight);
        for (size_t i = 0; i < itemLabels.size(); ++i)
        {
            if ((int)i == selectedIndex)
            {
                continue;
            }
            itemLabels[i]->setBounds(0, rowHeight * (int)i, getWidth(),
                                     rowHeight);
        }
        return;
    }

    int y = 0;
    for (auto *label : itemLabels)
    {
        label->setBounds(0, y, getWidth(), rowHeight);
        y += rowHeight;
    }
}

void DropdownMenu::onDraw(SDL_Renderer *renderer)
{
    Helpers::fillRect(renderer, getLocalBoundsF(), Colors::border);
    SDL_FRect inner = getLocalBoundsF();
    inner.x += 1.0f;
    inner.y += 1.0f;
    inner.w -= 2.0f;
    inner.h -= 2.0f;
    Helpers::fillRect(renderer, inner, Colors::background);
}

bool DropdownMenu::mouseDown(const MouseEvent &event)
{
    if (!event.buttonState.left)
    {
        return false;
    }

    if (items.empty())
    {
        return true;
    }

    if (!expanded)
    {
        setExpanded(true);
        return true;
    }

    const int rowHeight = getRowHeight();
    const int row = rowHeight > 0 ? (event.mouseYi / rowHeight) : 0;
    if (row >= 0 && row < (int)items.size())
    {
        const int oldIndex = selectedIndex;
        setSelectedIndex(row);
        if (onSelectionChanged && selectedIndex != oldIndex)
        {
            onSelectionChanged(selectedIndex);
        }
    }

    setExpanded(false);
    return true;
}
