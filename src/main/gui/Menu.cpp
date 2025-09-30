#include "Menu.h"

#include "MenuBar.h"

#include "../CupuacuState.h"

#include "text.h"

Menu::Menu(CupuacuState *state, const std::string menuNameToUse, const std::function<void()> actionToUse) :
    Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse), action(actionToUse)
{
    disableParentClipping();
}

void Menu::showSubMenus()
{
    if (subMenus.empty())
        return;

    int subMenuYPos = getHeight();

    const int baseH = int(state->menuFontSize * state->pixelScale * 1.2f);
    const int baseW = int(state->menuFontSize * state->pixelScale * 10.0f);

    for (auto &subMenu : subMenus)
    {
        int w = baseW;
        int h = baseH;
        subMenu->setBounds(0, subMenuYPos, w, h);
        subMenuYPos += h;
    }

    currentlyOpen = true;
}

void Menu::hideSubMenus()
{
    for (auto &subMenu : subMenus)
    {
        subMenu->setBounds(0, 0, 0, 0);
    }

    currentlyOpen = false;
}

void Menu::onDraw(SDL_Renderer *renderer)
{
    const uint8_t bg = isMouseOver() ? 80 : 40;
    SDL_SetRenderDrawColor(renderer, bg, bg, bg, 255);
    auto rect = getBounds();
    SDL_RenderFillRect(renderer, &rect);
    const uint8_t fontPointSize = state->menuFontSize / state->pixelScale;
    renderText(renderer, menuName, fontPointSize, rect, depthIs0);
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
        state->rootComponent->setDirtyRecursive();
        return true;
    }

    if (wasCurrentlyOpen)
    {
        hideSubMenus();
        state->menuBar->hideSubMenus();
        state->rootComponent->setDirtyRecursive();
        return true;
    }

    state->menuBar->hideSubMenus();
    state->rootComponent->setDirtyRecursive();
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
}

void Menu::mouseEnter()
{
    setDirty();
}

