#include "Menu.h"

#include "MenuBar.h"
#include "Label.h"

#include "RoundedRect.h"

Menu::Menu(CupuacuState *state, const std::string menuNameToUse, const std::function<void()> actionToUse) :
    Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse), action(actionToUse)
{
    disableParentClipping();
    label = emplaceChild<Label>(state, menuName);
    label->setInterceptMouseEnabled(false);
    label->setFontSize(state->menuFontSize);
}

bool Menu::isFirstLevel() const
{
    return dynamic_cast<MenuBar*>(getParent()) != nullptr;
}

void Menu::resized()
{
    label->setBounds(0, 0, getWidth(), getHeight());

    if (isFirstLevel())
    {
        label->setCenterHorizontally(true);
    }
}

void Menu::showSubMenus()
{
    if (subMenus.empty())
    {
        return;
    }

    int subMenuYPos = getHeight();

    const int baseH = int(((float)state->menuFontSize / state->pixelScale) * 2.0f);
    const int baseW = int(((float)state->menuFontSize / state->pixelScale) * 12.0f);

    for (auto &subMenu : subMenus)
    {
        int w = baseW;
        int h = baseH;
        subMenu->setBounds(0, subMenuYPos, w, h);
        subMenu->setVisible(true);
        subMenuYPos += h;
    }

    // compute bounding rect
    int x = subMenus.front()->getXPos();
    int y = subMenus.front()->getYPos();
    int w = 0;
    int h = 0;

    for (auto* sub : subMenus)
    {
        if (sub->getWidth() > w) w = sub->getWidth();
        h = sub->getYPos() + sub->getHeight() - y;
    }

    currentlyOpen = true;
    setDirty();
}

void Menu::hideSubMenus()
{
    for (auto &subMenu : subMenus)
    {
        subMenu->setVisible(false);
    }

    currentlyOpen = false;
    setDirty();
}

void Menu::onDraw(SDL_Renderer* renderer)
{
    auto radius = 14.f/state->pixelScale;
    auto rect = getLocalBoundsF();

    if (isFirstLevel())
    {
        SDL_Color bg = Colors::background;
        Helpers::setRenderDrawColor(renderer, bg);

        auto rect = getLocalBoundsF();
        
        SDL_RenderFillRect(renderer, &rect);
        
        if (currentlyOpen)
        {
            SDL_Color col1 = { 70, 70, 70, 255 };
            drawRoundedRect(renderer, rect, radius, col1);
        }
        
        return;
    }

    SDL_Color col1 = { 50, 50, 50, 255 };
    SDL_Color col2 = { 60, 60, 200, 255 }; 
    SDL_Color outline { 180, 180, 180, 255 };

    auto parentMenu = dynamic_cast<Menu*>(getParent());
    bool isFirst = parentMenu->subMenus.front() == this;
    bool isLast  = parentMenu->subMenus.back()  == this;

    auto rectShrunk = rect;

    float shrink = 6.f / state->pixelScale;
    rectShrunk.x += shrink;
    rectShrunk.y += shrink;
    rectShrunk.w -= shrink*2;
    rectShrunk.h -= shrink*2;

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

    if (isMouseOver())
    {
        drawRoundedRect(renderer, rectShrunk, radius, col2);
    }
}

bool Menu::mouseDown(const MouseEvent &e)
{
    const bool wasCurrentlyOpen = currentlyOpen;

    if (subMenus.empty())
    {
        if (action)
        {
            action();
        }
        state->menuBar->hideSubMenus();
        state->rootComponent->setDirty();
        return true;
    }

    if (wasCurrentlyOpen)
    {
        hideSubMenus();
        state->menuBar->hideSubMenus();
        state->rootComponent->setDirty();
        return true;
    }

    state->menuBar->hideSubMenus();
    state->rootComponent->setDirty();
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

    if (dynamic_cast<Menu*>(state->componentUnderMouse) == nullptr)
    {
        if (state->componentUnderMouse == state->menuBar || state->menuBar->hasChild(state->componentUnderMouse))
        {
            state->menuBar->hideSubMenus();
            state->menuBar->setOpenSubMenuOnMouseOver(true);
        }
    }
}

void Menu::mouseEnter()
{
    if (!subMenus.empty() &&
        ((state->menuBar->getOpenMenu() != nullptr &&
        state->menuBar->getOpenMenu() != this) || state->menuBar->shouldOpenSubMenuOnMouseOver())) 
    {
        state->menuBar->hideSubMenus();
        showSubMenus();
    }

    setDirty();
}

