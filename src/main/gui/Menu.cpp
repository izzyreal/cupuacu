#include "gui/Menu.h"

#include "gui/MenuBar.h"
#include "gui/Label.h"
#include "gui/Window.h"
#include "gui/Helpers.h"
#include "gui/Colors.h"

#include "gui/text.h"

#include "gui/RoundedRect.h"

using namespace cupuacu::gui;

Menu::Menu(State *state, const std::string &menuNameToUse,
           const std::function<void()> &actionToUse)
    : Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse),
      action(actionToUse)
{
    disableParentClipping();
    label = emplaceChild<Label>(state);
    label->setInterceptMouseEnabled(false);
    label->setFontSize(state->menuFontSize);
}

Menu::Menu(State *state,
           const std::function<std::string()> &menuNameGetterToUse,
           const std::function<void()> &actionToUse)
    : Component(state, "Menu"), menuName(""), action(actionToUse),
      menuNameGetter(menuNameGetterToUse)
{
    disableParentClipping();
    label = emplaceChild<Label>(state);
    label->setInterceptMouseEnabled(false);
    label->setFontSize(state->menuFontSize);
}

void Menu::setIsAvailable(const std::function<bool()> &isAvailableToUse)
{
    isAvailable = isAvailableToUse;
}

std::string Menu::getMenuName() const
{
    if (menuName.empty())
    {
        return menuNameGetter();
    }
    return menuName;
}

bool Menu::isFirstLevel() const
{
    return dynamic_cast<MenuBar *>(getParent()) != nullptr;
}

void Menu::resized()
{
    label->setBounds(0, 0, getWidth(), getHeight());

    if (isFirstLevel())
    {
        label->setCenterHorizontally(true);
    }
    else
    {
        label->setMargin(32);
    }
}

void Menu::showSubMenus()
{
    if (subMenus.empty())
    {
        return;
    }

    int subMenuYPos = getHeight();

    const int menuItemHeight =
        int((float)state->menuFontSize / state->pixelScale * 2.0f);

    int subMenuWidth = 1;

    for (const auto &subMenu : subMenus)
    {
        const auto subMenuName = subMenu->getMenuName();
        auto [tw, th] = measureText(subMenuName, state->menuFontSize);
        subMenuWidth = std::max(subMenuWidth, tw);
    }

    const int subMenuHorizontalMargin = 64.f / state->pixelScale;

    for (const auto &subMenu : subMenus)
    {
        subMenu->setBounds(0, subMenuYPos,
                           subMenuWidth + subMenuHorizontalMargin,
                           menuItemHeight);
        subMenu->setVisible(true);
        subMenuYPos += menuItemHeight;
    }

    currentlyOpen = true;
    setDirty();
}

void Menu::hideSubMenus()
{
    for (const auto &subMenu : subMenus)
    {
        subMenu->setVisible(false);
    }

    currentlyOpen = false;
    setDirty();
}

void Menu::onDraw(SDL_Renderer *renderer)
{
    const auto radius = 14.f / state->pixelScale;
    const auto rect = getLocalBoundsF();

    label->setOpacity(isAvailable() ? 255 : 128);

    label->setText(getMenuName());

    if (isFirstLevel())
    {
        constexpr SDL_Color bg = Colors::background;
        Helpers::setRenderDrawColor(renderer, bg);

        SDL_RenderFillRect(renderer, &rect);

        if (currentlyOpen)
        {
            constexpr SDL_Color col1 = {70, 70, 70, 255};
            drawRoundedRect(renderer, rect, radius, col1);
        }

        return;
    }

    constexpr SDL_Color col1 = {50, 50, 50, 255};
    constexpr SDL_Color outline{180, 180, 180, 255};

    const auto parentMenu = dynamic_cast<Menu *>(getParent());
    const bool isFirst = parentMenu->subMenus.front() == this;
    const bool isLast = parentMenu->subMenus.back() == this;

    auto rectShrunk = rect;

    const float shrink = 6.f / state->pixelScale;
    rectShrunk.x += shrink;
    rectShrunk.y += shrink;
    rectShrunk.w -= shrink * 2;
    rectShrunk.h -= shrink * 2;

    if (isFirst && isLast)
    {
        drawRoundedRect(renderer, rect, radius, col1);
        drawRoundedRectOutline(renderer, rect, radius, outline);
    }
    else if (isFirst)
    {
        drawTopRoundedRect(renderer, rect, radius, col1);
        drawTopRoundedRectOutline(renderer, rect, radius, outline);
    }
    else if (isLast)
    {
        drawBottomRoundedRect(renderer, rect, radius, col1);
        drawBottomRoundedRectOutline(renderer, rect, radius, outline);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, col1.r, col1.g, col1.b, col1.a);
        SDL_RenderFillRect(renderer, &rect);
        drawVerticalEdges(renderer, rect, outline);
    }

    if (isMouseOver() && isAvailable())
    {
        constexpr SDL_Color col2 = {60, 60, 200, 255};
        drawRoundedRect(renderer, rectShrunk, radius, col2);
    }
}

bool Menu::mouseDown(const MouseEvent &e)
{
    const bool wasCurrentlyOpen = currentlyOpen;

    if (subMenus.empty())
    {
        if (!isAvailable())
        {
            return true;
        }
        if (action)
        {
            if (const auto window = getWindow(); window && window->getMenuBar())
            {
                window->getMenuBar()->hideSubMenus();
            }
            action();
        }
        else
        {
            if (const auto window = getWindow(); window && window->getMenuBar())
            {
                window->getMenuBar()->hideSubMenus();
            }
        }
        return true;
    }

    if (wasCurrentlyOpen)
    {
        hideSubMenus();
        if (const auto window = getWindow(); window && window->getMenuBar())
        {
            window->getMenuBar()->hideSubMenus();
            if (const auto root = window->getRootComponent())
            {
                root->setDirty();
            }
        }
        return true;
    }

    if (const auto window = getWindow(); window && window->getMenuBar())
    {
        window->getMenuBar()->hideSubMenus();
        if (const auto root = window->getRootComponent())
        {
            root->setDirty();
        }
    }
    showSubMenus();

    return true;
}

bool Menu::mouseUp(const MouseEvent &e)
{
    return true;
}

void Menu::mouseLeave()
{
    setDirty();

    const auto window = getWindow();
    if (!window)
    {
        return;
    }

    if (dynamic_cast<Menu *>(window->getComponentUnderMouse()) == nullptr)
    {
        const bool componentUnderMouseIsMenuBar =
            window->getComponentUnderMouse() == window->getMenuBar();
        const bool componentUnderMouseIsMenuBarChild =
            window->getMenuBar() &&
            window->getMenuBar()->hasChild(window->getComponentUnderMouse());
        if (componentUnderMouseIsMenuBar || componentUnderMouseIsMenuBarChild)
        {
            if (window->getMenuBar() && window->getMenuBar()->hasMenuOpen())
            {
                window->getMenuBar()->hideSubMenus();
                window->getMenuBar()->setOpenSubMenuOnMouseOver(true);
            }
        }
    }
}

void Menu::mouseEnter()
{
    const auto window = getWindow();
    if (!window || !window->getMenuBar())
    {
        return;
    }

    if (!subMenus.empty() &&
        ((window->getMenuBar()->getOpenMenu() != nullptr &&
          window->getMenuBar()->getOpenMenu() != this) ||
         window->getMenuBar()->shouldOpenSubMenuOnMouseOver()))
    {
        window->getMenuBar()->hideSubMenus();
        showSubMenus();
    }

    setDirty();
}
