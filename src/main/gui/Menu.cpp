#include "gui/Menu.hpp"

#include "gui/MenuBar.hpp"
#include "gui/MenuPlanning.hpp"
#include "gui/Label.hpp"
#include "gui/Window.hpp"
#include "gui/Helpers.hpp"
#include "gui/Colors.hpp"

#include "gui/text.hpp"

#include "gui/RoundedRect.hpp"
#include "gui/UiScale.hpp"

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

bool Menu::shouldShowAsSubMenuItem() const
{
    return !getMenuName().empty();
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
        scaleUi(state, static_cast<float>(state->menuFontSize) * 2.0f);

    int subMenuWidth = 1;

    for (const auto &subMenu : subMenus)
    {
        if (!subMenu->shouldShowAsSubMenuItem())
        {
            continue;
        }
        const auto subMenuName = subMenu->getMenuName();
        auto [tw, th] = measureText(subMenuName, state->menuFontSize);
        subMenuWidth = std::max(subMenuWidth, tw);
    }

    const int subMenuHorizontalMargin = scaleUi(state, 64.0f);

    for (const auto &subMenu : subMenus)
    {
        if (!subMenu->shouldShowAsSubMenuItem())
        {
            subMenu->setVisible(false);
            continue;
        }
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
    const auto radius = scaleUiF(state, 14.0f);
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

    const float shrink = scaleUiF(state, 6.0f);
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
    const auto plan =
        planMenuMouseDown(subMenus.empty() == false, currentlyOpen, isAvailable());
    if (const auto window = getWindow(); window && window->getMenuBar())
    {
        if (plan.hideMenuBarSubMenus)
        {
            window->getMenuBar()->hideSubMenus();
            if (const auto root = window->getRootComponent())
            {
                root->setDirty();
            }
        }
    }
    if (plan.hideSelfSubMenus)
    {
        hideSubMenus();
    }
    if (plan.showSelfSubMenus)
    {
        showSubMenus();
    }
    if (plan.invokeAction && action)
    {
        action();
    }
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
        const auto plan = planMenuMouseLeave(
            componentUnderMouseIsMenuBar || componentUnderMouseIsMenuBarChild,
            window->getMenuBar() && window->getMenuBar()->hasMenuOpen());
        if (plan.hideMenuBarSubMenus)
        {
            window->getMenuBar()->hideSubMenus();
        }
        if (plan.enableOpenOnHover)
        {
            window->getMenuBar()->setOpenSubMenuOnMouseOver(true);
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

    const auto plan = planMenuMouseEnter(
        !subMenus.empty(),
        window->getMenuBar()->getOpenMenu() != nullptr &&
            window->getMenuBar()->getOpenMenu() != this,
        window->getMenuBar()->shouldOpenSubMenuOnMouseOver());
    if (plan.hideMenuBarSubMenus)
    {
        window->getMenuBar()->hideSubMenus();
    }
    if (plan.showSelfSubMenus)
    {
        showSubMenus();
    }

    setDirty();
}
