#include "Menu.h"

#include "MenuBar.h"

#include "../CupuacuState.h"

#include "text.h"

Menu::Menu(CupuacuState *state, const std::string menuNameToUse, const std::function<void()> actionToUse) :
    Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse), action(actionToUse)
{
}

void Menu::showSubMenus()
{
    if (subMenus.empty())
        return;

    float scale = 4.0f / state->pixelScale;
    int subMenuYPos = getHeight();

    for (auto &subMenu : subMenus)
    {
        int w = int(150 * scale);
        int h = int(20 * scale);
        subMenu->setBounds(int(5 * scale), subMenuYPos, w, h);
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
    SDL_FRect r{0, 0, (float)getWidth(), (float)getHeight() };
    SDL_RenderFillRect(renderer, &r);
    const uint8_t fontPointSize = state->menuFontSize / state->pixelScale;
    renderText(renderer, menuName, fontPointSize);
}

bool Menu::mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY)
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

bool Menu::mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY)
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

