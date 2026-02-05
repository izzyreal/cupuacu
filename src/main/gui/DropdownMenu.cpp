#include "DropdownMenu.h"

#include "Colors.h"
#include "Helpers.h"
#include "RoundedRect.h"
#include "Window.h"
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
    if (!expanded)
    {
        hoveredIndex = -1;
    }
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
    SDL_FRect outer = getLocalBoundsF();
    const float radius = std::max(1.0f, 12.0f / state->pixelScale);

    const SDL_Color border = Colors::border;
    const SDL_Color baseBg = {55, 55, 55, 255};
    const SDL_Color hoverBg = {70, 70, 70, 255};

    drawRoundedRect(renderer, outer, radius, border);

    SDL_FRect inner = outer;
    inner.x += 1.0f;
    inner.y += 1.0f;
    inner.w -= 2.0f;
    inner.h -= 2.0f;
    drawRoundedRect(renderer, inner, std::max(0.0f, radius - 1.0f), baseBg);

    if (expanded && hoveredIndex >= 0 &&
        hoveredIndex < (int)itemLabels.size())
    {
        const int rowHeight = getRowHeight();
        SDL_FRect hoverRect = inner;
        hoverRect.y = inner.y + hoveredIndex * rowHeight;
        hoverRect.h = rowHeight;
        float hoverRadius = 0.0f;
        if (hoveredIndex == 0 || hoveredIndex == (int)itemLabels.size() - 1)
        {
            hoverRadius = std::max(0.0f, radius - 1.0f);
        }
        drawRoundedRect(renderer, hoverRect, hoverRadius, hoverBg);
    }

    if (itemLabels.size() > 1)
    {
        const float arrowPadding =
            std::max(1.0f, 12.0f / state->pixelScale);
        const float arrowWidth =
            std::max(1.0f, 20.0f / state->pixelScale);
        const float arrowHeight =
            std::max(1.0f, 10.0f / state->pixelScale);
        const float arrowOffset = std::max(1.0f, 4.0f / state->pixelScale);
        const float cx =
            std::round(inner.x + inner.w - arrowPadding - arrowWidth * 0.5f -
                       arrowOffset);
        const float cy = inner.y + getRowHeight() * 0.5f;
        SDL_Vertex verts[3];
        SDL_FColor color{220 / 255.f, 220 / 255.f, 220 / 255.f, 1.0f};
        verts[0].position = {cx - arrowWidth * 0.5f, cy - arrowHeight * 0.5f};
        verts[1].position = {cx + arrowWidth * 0.5f, cy - arrowHeight * 0.5f};
        verts[2].position = {cx, cy + arrowHeight * 0.5f};
        for (auto &v : verts)
        {
            v.color = color;
            v.tex_coord = {0.f, 0.f};
        }
        const int indices[3] = {0, 1, 2};
        SDL_RenderGeometry(renderer, nullptr, verts, 3, indices, 3);
    }
}

bool DropdownMenu::mouseMove(const MouseEvent &event)
{
    if (!expanded)
    {
        if (hoveredIndex != -1)
        {
            hoveredIndex = -1;
            setDirty();
        }
        return false;
    }

    const int rowHeight = getRowHeight();
    const int row = rowHeight > 0 ? (event.mouseYi / rowHeight) : -1;
    const int newIndex =
        (row >= 0 && row < (int)itemLabels.size()) ? row : -1;
    if (newIndex != hoveredIndex)
    {
        hoveredIndex = newIndex;
        setDirty();
    }
    return false;
}

void DropdownMenu::mouseLeave()
{
    if (hoveredIndex != -1)
    {
        hoveredIndex = -1;
        setDirty();
    }
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
        if (items.size() > 1)
        {
            setExpanded(true);
        }
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
