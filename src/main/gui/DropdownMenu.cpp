#include "DropdownMenu.hpp"

#include "Colors.hpp"
#include "Helpers.hpp"
#include "RoundedRect.hpp"
#include "UiScale.hpp"
#include "Window.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace cupuacu::gui;

namespace
{
    constexpr Uint32 getPopupHighDensityWindowFlag()
    {
#if defined(__linux__)
        return 0;
#else
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    }

    class DropdownPopupContent : public Component, public DropdownOwnerComponent
    {
    public:
        DropdownPopupContent(cupuacu::State *stateToUse,
                             DropdownMenu *ownerToUse,
                             std::vector<std::string> itemsToUse,
                             const int fontSizeToUse, const int itemMarginToUse,
                             const int selectedIndexToUse,
                             std::function<void(int)> onSelectedToUse)
            : Component(stateToUse, "DropdownPopupContent"),
              owner(ownerToUse), items(std::move(itemsToUse)),
              fontSize(fontSizeToUse), itemMargin(itemMarginToUse),
              selectedIndex(selectedIndexToUse),
              onSelected(std::move(onSelectedToUse))
        {
            for (const auto &item : items)
            {
                auto *label = emplaceChild<Label>(state, item);
                label->setFontSize(fontSize);
                label->setMargin(itemMargin * state->pixelScale);
                label->setCenterHorizontally(false);
                label->setOpacity(160);
                itemLabels.push_back(label);
            }

            if (selectedIndex >= 0 &&
                selectedIndex < static_cast<int>(itemLabels.size()))
            {
                itemLabels[selectedIndex]->setOpacity(255);
            }
        }

        DropdownMenu *getOwningDropdown() const override
        {
            return owner;
        }

        int getRowHeight() const
        {
            return owner ? owner->getPopupRowHeight() : 0;
        }

        void resized() override
        {
            const int rowHeight = getRowHeight();
            int y = 0;
            for (auto *label : itemLabels)
            {
                label->setBounds(0, y, getWidth(), rowHeight);
                y += rowHeight;
            }
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const SDL_FRect outer = getLocalBoundsF();
            const float radius = scaleUiF(state, 12.0f);

            constexpr SDL_Color border = Colors::border;
            constexpr SDL_Color baseBg = {55, 55, 55, 255};
            constexpr SDL_Color hoverBg = {70, 70, 70, 255};

            drawRoundedRect(renderer, outer, radius, border);

            SDL_FRect inner = outer;
            inner.x += 1.0f;
            inner.y += 1.0f;
            inner.w -= 2.0f;
            inner.h -= 2.0f;
            drawRoundedRect(renderer, inner, std::max(0.0f, radius - 1.0f),
                            baseBg);

            const int rowHeight = getRowHeight();
            if (hoveredIndex >= 0 && hoveredIndex < static_cast<int>(items.size()))
            {
                SDL_FRect hoverRect = inner;
                hoverRect.y = inner.y + hoveredIndex * rowHeight;
                hoverRect.h = rowHeight;
                float hoverRadius = 0.0f;
                if (hoveredIndex == 0 ||
                    hoveredIndex == static_cast<int>(items.size()) - 1)
                {
                    hoverRadius = std::max(0.0f, radius - 1.0f);
                }
                drawRoundedRect(renderer, hoverRect, hoverRadius, hoverBg);
            }
        }

        bool mouseMove(const MouseEvent &event) override
        {
            const int rowHeight = getRowHeight();
            const int row = rowHeight > 0 ? event.mouseYi / rowHeight : -1;
            const int newIndex =
                row >= 0 && row < static_cast<int>(items.size()) ? row : -1;
            if (newIndex != hoveredIndex)
            {
                hoveredIndex = newIndex;
                setDirty();
            }
            return true;
        }

        void mouseLeave() override
        {
            if (hoveredIndex != -1)
            {
                hoveredIndex = -1;
                setDirty();
            }
        }

        bool mouseDown(const MouseEvent &event) override
        {
            if (!event.buttonState.left)
            {
                return false;
            }

            const int rowHeight = getRowHeight();
            const int row = rowHeight > 0 ? event.mouseYi / rowHeight : -1;
            pressedIndex =
                row >= 0 && row < static_cast<int>(items.size()) ? row : -1;
            return true;
        }

        bool mouseUp(const MouseEvent &event) override
        {
            if (!event.buttonState.left)
            {
                pressedIndex = -1;
                return false;
            }

            const int rowHeight = getRowHeight();
            const int row = rowHeight > 0 ? event.mouseYi / rowHeight : -1;
            const int committedIndex = pressedIndex;
            pressedIndex = -1;
            if (row >= 0 && row == committedIndex &&
                row < static_cast<int>(items.size()) && onSelected)
            {
                onSelected(row);
                return true;
            }
            return true;
        }

    private:
        DropdownMenu *owner = nullptr;
        std::vector<std::string> items;
        std::vector<Label *> itemLabels;
        int fontSize = 0;
        int itemMargin = 0;
        int selectedIndex = -1;
        int hoveredIndex = -1;
        int pressedIndex = -1;
        std::function<void(int)> onSelected;
    };

    void removePopupWindowFromState(cupuacu::State *state, Window *popupWindow)
    {
        if (!state || !popupWindow)
        {
            return;
        }

        const auto it =
            std::find(state->windows.begin(), state->windows.end(), popupWindow);
        if (it != state->windows.end())
        {
            state->windows.erase(it);
        }
    }
} // namespace

DropdownMenu::DropdownMenu(State *stateToUse)
    : Component(stateToUse, "DropdownMenu")
{
}

DropdownMenu::~DropdownMenu()
{
    destroyPopupWindow();
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
    const int fontSizeVirtual = scaleFontPointSize(state, getFontSize());
    const auto [textW, textH] = measureText("Ag", fontSizeVirtual);
    const int textHeight = std::max(1, (int)std::ceil(textH));
    return textHeight + scaleUi(state, static_cast<float>(itemMargin) * 2.0f);
}

int DropdownMenu::getPopupRowHeight() const
{
    if (collapsedHeight > 0)
    {
        return collapsedHeight;
    }
    return getRowHeight();
}

void DropdownMenu::rebuildLabels()
{
    removeChildrenOfType<Label>();
    itemLabels.clear();

    const int fontSize = getFontSize();
    for (const auto &item : items)
    {
        auto *label = emplaceChild<Label>(state, item);
        label->setFontSize(fontSize);
        label->setMargin(itemMargin * state->pixelScale);
        label->setCenterHorizontally(false);
        itemLabels.push_back(label);
    }
}

void DropdownMenu::updateLabelStyles() const
{
    for (size_t i = 0; i < itemLabels.size(); ++i)
    {
        const bool isSelected = static_cast<int>(i) == selectedIndex;
        itemLabels[i]->setOpacity(isSelected ? 255 : 160);
    }
}

void DropdownMenu::updateLabelVisibility() const
{
    for (size_t i = 0; i < itemLabels.size(); ++i)
    {
        const bool shouldShow = static_cast<int>(i) == selectedIndex;
        itemLabels[i]->setVisible(shouldShow);
    }
}

void DropdownMenu::destroyPopupWindow()
{
    if (!popupWindow)
    {
        return;
    }

    if (!popupWindow->isOpen())
    {
        removePopupWindowFromState(state, popupWindow.get());
        popupWindow.reset();
        return;
    }

    if (popupWindow->isDispatching())
    {
        popupWindow->requestClose();
        return;
    }

    removePopupWindowFromState(state, popupWindow.get());
    popupWindow.reset();
}

void DropdownMenu::ensurePopupWindow()
{
    if (popupWindow && !popupWindow->isOpen())
    {
        popupWindow.reset();
    }

    if (popupWindow || !window || !window->getSdlWindow() || items.empty())
    {
        return;
    }

    const auto [absoluteX, absoluteY] = getAbsolutePosition();
    const int rowHeight = getPopupRowHeight();
    const int popupHeight =
        std::max(1, static_cast<int>(items.size())) * rowHeight;
    const int verticalAnchorOffset = std::max(0, (getHeight() - rowHeight) / 2);
    const int popupY =
        absoluteY + verticalAnchorOffset - std::max(0, selectedIndex) * rowHeight;

    const SDL_Rect popupCanvasBounds{absoluteX, popupY, getWidth(), popupHeight};
    const SDL_Rect popupScreenBounds =
        window->mapCanvasRectToScreenRect(popupCanvasBounds);
    if (popupScreenBounds.w <= 0 || popupScreenBounds.h <= 0)
    {
        return;
    }

    int parentWindowX = 0;
    int parentWindowY = 0;
    SDL_GetWindowPosition(window->getSdlWindow(), &parentWindowX, &parentWindowY);
    const int offsetX = popupScreenBounds.x - parentWindowX;
    const int offsetY = popupScreenBounds.y - parentWindowY;

    popupWindow = std::make_unique<Window>(
        state, "", popupScreenBounds.w, popupScreenBounds.h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY |
            SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_TRANSPARENT |
            SDL_WINDOW_HIDDEN | getPopupHighDensityWindowFlag());
    if (!popupWindow || !popupWindow->isOpen())
    {
        popupWindow.reset();
        return;
    }

    if (!SDL_SetWindowParent(popupWindow->getSdlWindow(), window->getSdlWindow()))
    {
        SDL_Log("DropdownMenu: SDL_SetWindowParent failed: %s", SDL_GetError());
    }
    if (!SDL_SetWindowPosition(popupWindow->getSdlWindow(), popupScreenBounds.x,
                               popupScreenBounds.y))
    {
        SDL_Log("DropdownMenu: SDL_SetWindowPosition failed: %s",
                SDL_GetError());
    }
    if (!SDL_SetWindowAlwaysOnTop(popupWindow->getSdlWindow(), true))
    {
        SDL_Log("DropdownMenu: SDL_SetWindowAlwaysOnTop failed: %s",
                SDL_GetError());
    }

    if (!popupWindow->setCanvasSize(popupCanvasBounds.w, popupCanvasBounds.h))
    {
        popupWindow.reset();
        return;
    }

    auto root = std::make_unique<DropdownPopupContent>(
        state, this, items, getFontSize(), itemMargin, selectedIndex,
        [this](const int index) { handlePopupSelection(index); });
    root->setBounds(0, 0, popupCanvasBounds.w, popupCanvasBounds.h);
    popupWindow->setRootComponent(std::move(root));
    popupWindow->setOnClose([this, popup = popupWindow.get()]()
                            { removePopupWindowFromState(state, popup); });
    if (state)
    {
        state->windows.push_back(popupWindow.get());
    }
    SDL_ShowWindow(popupWindow->getSdlWindow());
    popupWindow->renderFrame();
}

void DropdownMenu::handlePopupSelection(const int index)
{
    const int oldIndex = selectedIndex;
    setSelectedIndex(index);
    if (onSelectionChanged && selectedIndex != oldIndex)
    {
        onSelectionChanged(selectedIndex);
    }
    setExpanded(false);
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
    const int targetHeight =
        collapsedHeight > 0 ? std::max(collapsedHeight, getRowHeight())
                            : getRowHeight();
    setSize(getWidth(), targetHeight);
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
    const int targetHeight =
        collapsedHeight > 0 ? std::max(collapsedHeight, getRowHeight())
                            : getRowHeight();
    setSize(getWidth(), targetHeight);
    resized();
    setDirty();
}

void DropdownMenu::setOnSelectionChanged(std::function<void(int)> callback)
{
    onSelectionChanged = std::move(callback);
}

void DropdownMenu::setCollapsedHeight(const int height)
{
    collapsedHeight = std::max(height, getRowHeight());
    if (collapsedHeight > 0)
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
    hoveredIndex = -1;

    if (expanded)
    {
        previousFocusedComponent = window ? window->getFocusedComponent() : nullptr;
        if (window)
        {
            window->setFocusedComponent(this);
        }
        ensurePopupWindow();
    }
    else
    {
        destroyPopupWindow();
        if (window && window->getFocusedComponent() == this)
        {
            Component *restore = previousFocusedComponent;
            if (restore != nullptr && window->getRootComponent() != nullptr &&
                !Component::isComponentOrChildOf(restore,
                                                 window->getRootComponent()))
            {
                restore = nullptr;
            }
            window->setFocusedComponent(restore);
        }
        previousFocusedComponent = nullptr;
    }

    setDirty();
}

void DropdownMenu::resized()
{
    const int rowHeight = getRowHeight();
    if (selectedIndex >= 0 && selectedIndex < (int)itemLabels.size())
    {
        itemLabels[selectedIndex]->setBounds(0, 0, getWidth(), getHeight());
    }

    for (size_t i = 0; i < itemLabels.size(); ++i)
    {
        if ((int)i == selectedIndex)
        {
            continue;
        }
        itemLabels[i]->setBounds(0, rowHeight * (int)i, getWidth(), rowHeight);
    }
}

void DropdownMenu::onDraw(SDL_Renderer *renderer)
{
    const SDL_FRect outer = getLocalBoundsF();
    const float radius = scaleUiF(state, 12.0f);

    constexpr SDL_Color border = Colors::border;
    constexpr SDL_Color baseBg = {55, 55, 55, 255};

    drawRoundedRect(renderer, outer, radius, border);

    SDL_FRect inner = outer;
    inner.x += 1.0f;
    inner.y += 1.0f;
    inner.w -= 2.0f;
    inner.h -= 2.0f;
    drawRoundedRect(renderer, inner, std::max(0.0f, radius - 1.0f), baseBg);

    if (itemLabels.size() > 1 && !expanded)
    {
        const float arrowPadding = scaleUiF(state, 12.0f);
        const float arrowWidth = scaleUiF(state, 20.0f);
        const float arrowHeight = scaleUiF(state, 10.0f);
        const float arrowOffset = scaleUiF(state, 4.0f);
        const float cx = std::round(inner.x + inner.w - arrowPadding -
                                    arrowWidth * 0.5f - arrowOffset);
        const float cy = inner.y + inner.h * 0.5f;
        SDL_Vertex verts[3];
        constexpr SDL_FColor color{220 / 255.f, 220 / 255.f, 220 / 255.f, 1.0f};
        verts[0].position = {cx - arrowWidth * 0.5f, cy - arrowHeight * 0.5f};
        verts[1].position = {cx + arrowWidth * 0.5f, cy - arrowHeight * 0.5f};
        verts[2].position = {cx, cy + arrowHeight * 0.5f};
        for (auto &v : verts)
        {
            v.color = color;
            v.tex_coord = {0.f, 0.f};
        }
        constexpr int indices[3] = {0, 1, 2};
        SDL_RenderGeometry(renderer, nullptr, verts, 3, indices, 3);
    }
}

bool DropdownMenu::mouseMove(const MouseEvent &event)
{
    (void)event;
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

    setExpanded(false);
    return true;
}
