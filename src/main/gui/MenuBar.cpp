#include "MenuBar.h"

#include "../State.h"
#include "OpaqueRect.h"
#include "Menu.h"
#include "../ResourceUtil.hpp"

#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Save.h"

using namespace cupuacu::gui;

MenuBar::MenuBar(cupuacu::State *stateToUse) : Component(stateToUse, "MenuBar")
{
    background = emplaceChild<OpaqueRect>(state, Colors::background);
    disableParentClipping();

    // File and View menus
    fileMenu = emplaceChild<Menu>(state, "File");
    viewMenu = emplaceChild<Menu>(state, "View");

#ifdef __APPLE__
    const std::string openText{"Open (Cmd + O)"};
#else
    const std::string openText{"Open (Ctrl + O)"};
#endif
    fileMenu->addSubMenu(state, openText, [&]{ actions::showOpenFileDialog(state); });

#ifdef __APPLE__
    const std::string overwriteText{"Overwrite (Cmd + S)"};
#else
    const std::string overwriteText{"Overwrite (Ctrl + S)"};
#endif
    fileMenu->addSubMenu(state, overwriteText, [&]{ actions::overwrite(state); });

    viewMenu->addSubMenu(state, "Reset zoom (Esc)", [&]{ actions::resetZoom(state); });
    viewMenu->addSubMenu(state, "Zoom out horiz. (Q)", [&]{ actions::tryZoomOutHorizontally(state); });
    viewMenu->addSubMenu(state, "Zoom in horiz. (W)", [&]{ actions::tryZoomInHorizontally(state); });
    viewMenu->addSubMenu(state, "Zoom out vert. (E)", [&]{ actions::tryZoomOutVertically(state, 1); });
    viewMenu->addSubMenu(state, "Zoom in vert. (R)", [&]{ actions::zoomInVertically(state, 1); });

    // Edit menu
    editMenu = emplaceChild<Menu>(state, "Edit");

    std::function<std::string()> undoMenuNameGetter = [&] {
#ifdef __APPLE__
    const std::string undoShortcut = " (Cmd + Z)";
#else
    const std::string undoShortcut = " (Ctrl + Z)";
#endif
        auto description = state->getUndoDescription();
        if (!description.empty()) description.insert(0, " ");
        return "Undo" + description + undoShortcut;
    };

    auto undoMenu = editMenu->addSubMenu(state, undoMenuNameGetter, [&] { state->undo(); });

    undoMenu->setIsAvailable([&]{ return state->canUndo(); });

    std::function<std::string()> redoMenuNameGetter = [&] {
#ifdef __APPLE__
    const std::string redoShortcut = " (Cmd + Shift + Z)";
#else
    const std::string redoShortcut = " (Ctrl + Shift + Z)";
#endif
        auto description = state->getRedoDescription();
        if (!description.empty()) description.insert(0, " ");
        return "Redo" + description + redoShortcut;
    };

    auto redoMenu = editMenu->addSubMenu(state, redoMenuNameGetter, [&] { state->redo(); });

    redoMenu->setIsAvailable([&]{ return state->canRedo(); });

    logoData = get_resource_data("cupuacu-logo1.bmp");
}

void MenuBar::onDraw(SDL_Renderer* renderer)
{
    if (!logoTexture && !logoData.empty())
    {
        SDL_IOStream* io = SDL_IOFromConstMem(logoData.data(),
                                              static_cast<int>(logoData.size()));
        if (io)
        {
            SDL_Surface* surf = SDL_LoadBMP_IO(io, true);
            if (surf)
            {
                logoTexture = SDL_CreateTextureFromSurface(renderer, surf);
                logoW = surf->w;
                logoH = surf->h;
                SDL_SetTextureScaleMode(logoTexture, SDL_SCALEMODE_NEAREST);
                SDL_DestroySurface(surf);
                resized();
            }
        }
    }

    SDL_FRect dst;
    dst.x = 0.0f;
    dst.y = 0.0f;
    dst.w = float(logoW);
    dst.h = float(getHeight());

    float scale = float(getHeight()) / float(logoH);
    dst.w = logoW * scale;

    Helpers::fillRect(renderer, dst, Colors::background);
    SDL_RenderTexture(renderer, logoTexture, nullptr, &dst);
}

void MenuBar::hideSubMenus()
{
    fileMenu->hideSubMenus();
    viewMenu->hideSubMenus();
    editMenu->hideSubMenus();
    state->rootComponent->setDirty();
    openSubMenuOnMouseOver = false;
}

void MenuBar::resized()
{
    float scale = 4.0f / state->pixelScale;

    int fileW = int(40 * scale);
    int viewW = int(40 * scale);
    int editW = int(40 * scale); // wide enough for Undo/Redo text
    int h = getHeight();

    int logoSpace = 0;
    if (!logoData.empty())
    {
        float aspect = logoH ? (float)logoW / (float)logoH : 1.0f;
        logoSpace = int(h * aspect);
    }

    fileMenu->setBounds(logoSpace, 0, fileW, h);
    viewMenu->setBounds(logoSpace + fileW, 0, viewW, h);
    editMenu->setBounds(logoSpace + fileW + viewW, 0, editW, h);

    SDL_Rect backgroundBounds = getLocalBounds();
    backgroundBounds.x = editMenu->getBounds().x + editW;
    backgroundBounds.w = getWidth() - backgroundBounds.x;

    background->setBounds(backgroundBounds);
}

void MenuBar::mouseEnter()
{
    setDirty();
}

Menu* MenuBar::getOpenMenu()
{
    if (fileMenu->isOpen()) return fileMenu;
    if (viewMenu->isOpen()) return viewMenu;
    if (editMenu->isOpen()) return editMenu;
    return nullptr;
}

bool MenuBar::hasMenuOpen()
{
    return getOpenMenu() != nullptr;
}

