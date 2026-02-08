#include "MenuBar.hpp"

#include "State.hpp"
#include "ResourceUtil.hpp"

#include "gui/OpaqueRect.hpp"
#include "gui/Menu.hpp"
#include "gui/Window.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Colors.hpp"
#include "gui/Helpers.hpp"

#include "actions/ShowOpenFileDialog.hpp"
#include "actions/Save.hpp"
#include "actions/audio/Copy.hpp"
#include "actions/audio/Trim.hpp"
#include "actions/audio/Cut.hpp"
#include "actions/audio/Paste.hpp"
#include "actions/audio/EditCommands.hpp"

#include <memory>

using namespace cupuacu::gui;

MenuBar::MenuBar(State *stateToUse) : Component(stateToUse, "MenuBar")
{
    background = emplaceChild<OpaqueRect>(state, Colors::background);
    disableParentClipping();

    // File and View menus
    fileMenu = emplaceChild<Menu>(state, "File");
    viewMenu = emplaceChild<Menu>(state, "View");

#ifdef __APPLE__
    constexpr std::string openText{"Open (Cmd + O)"};
#else
    const std::string openText{"Open (Ctrl + O)"};
#endif
    fileMenu->addSubMenu(state, openText,
                         [&]
                         {
                             actions::showOpenFileDialog(state);
                         });

#ifdef __APPLE__
    constexpr std::string overwriteText{"Overwrite (Cmd + S)"};
#else
    const std::string overwriteText{"Overwrite (Ctrl + S)"};
#endif
    fileMenu->addSubMenu(state, overwriteText,
                         [&]
                         {
                             actions::overwrite(state);
                         });

    viewMenu->addSubMenu(state, "Reset zoom (Esc)",
                         [&]
                         {
                             actions::resetZoom(state);
                         });
    viewMenu->addSubMenu(state, "Zoom out horiz. (Q)",
                         [&]
                         {
                             actions::tryZoomOutHorizontally(state);
                         });
    viewMenu->addSubMenu(state, "Zoom in horiz. (W)",
                         [&]
                         {
                             actions::tryZoomInHorizontally(state);
                         });
    viewMenu->addSubMenu(state, "Zoom out vert. (E)",
                         [&]
                         {
                             actions::tryZoomOutVertically(state, 1);
                         });
    viewMenu->addSubMenu(state, "Zoom in vert. (R)",
                         [&]
                         {
                             actions::zoomInVertically(state, 1);
                         });

    // Edit menu
    editMenu = emplaceChild<Menu>(state, "Edit");
    optionsMenu = emplaceChild<Menu>(state, "Options");

    std::function<std::string()> undoMenuNameGetter = [&]
    {
#ifdef __APPLE__
        constexpr std::string undoShortcut = " (Cmd + Z)";
#else
        const std::string undoShortcut = " (Ctrl + Z)";
#endif
        auto description = state->getUndoDescription();
        if (!description.empty())
            description.insert(0, " ");
        return "Undo" + description + undoShortcut;
    };

    auto undoMenu = editMenu->addSubMenu(state, undoMenuNameGetter,
                                         [&]
                                         {
                                             state->undo();
                                         });

    undoMenu->setIsAvailable(
        [&]
        {
            return state->canUndo();
        });

    std::function<std::string()> redoMenuNameGetter = [&]
    {
#ifdef __APPLE__
        constexpr std::string redoShortcut = " (Cmd + Shift + Z)";
#else
        const std::string redoShortcut = " (Ctrl + Shift + Z)";
#endif
        auto description = state->getRedoDescription();
        if (!description.empty())
            description.insert(0, " ");
        return "Redo" + description + redoShortcut;
    };

    auto redoMenu = editMenu->addSubMenu(state, redoMenuNameGetter,
                                         [&]
                                         {
                                             state->redo();
                                         });

    redoMenu->setIsAvailable(
        [&]
        {
            return state->canRedo();
        });

    logoData = get_resource_data("cupuacu-logo1.bmp");

    // --- Trim, Cut, Copy, Paste ---

#ifdef __APPLE__
    constexpr std::string trimText{"Trim (Cmd + T)"};
    constexpr std::string cutText{"Cut (Cmd + X)"};
    constexpr std::string copyText{"Copy (Cmd + C)"};
    constexpr std::string pasteText{"Paste (Cmd + V)"};
#else
    const std::string trimText{"Trim (Ctrl + T)"};
    const std::string cutText{"Cut (Ctrl + X)"};
    const std::string copyText{"Copy (Ctrl + C)"};
    const std::string pasteText{"Paste (Ctrl + V)"};
#endif

    auto trimMenu = editMenu->addSubMenu(
        state, trimText,
        [&]
        {
            actions::audio::performTrim(state);
        });
    trimMenu->setIsAvailable(
        [&]
        {
            return state->activeDocumentSession.selection.isActive();
        });

    auto cutMenu = editMenu->addSubMenu(
        state, cutText,
        [&]
        {
            actions::audio::performCut(state);
        });
    cutMenu->setIsAvailable(
        [&]
        {
            return state->activeDocumentSession.selection.isActive();
        });

    auto copyMenu = editMenu->addSubMenu(
        state, copyText,
        [&]
        {
            actions::audio::performCopy(state);
        });
    copyMenu->setIsAvailable(
        [&]
        {
            return state->activeDocumentSession.selection.isActive();
        });

    auto pasteMenu = editMenu->addSubMenu(
        state, pasteText,
        [&]
        {
            actions::audio::performPaste(state);
        });
    pasteMenu->setIsAvailable(
        [&]
        {
            return state->clipboard.getFrameCount() > 0;
        });

    optionsMenu->addSubMenu(
        state, "Device Properties",
        [&]
        {
            if (!state->devicePropertiesWindow ||
                !state->devicePropertiesWindow->isOpen())
            {
                state->devicePropertiesWindow =
                    std::make_unique<DevicePropertiesWindow>(state);
            }
            else
            {
                state->devicePropertiesWindow->raise();
            }
        });
}

void MenuBar::onDraw(SDL_Renderer *renderer)
{
    if (!logoTexture && !logoData.empty())
    {
        SDL_IOStream *io = SDL_IOFromConstMem(
            logoData.data(), static_cast<int>(logoData.size()));
        if (io)
        {
            SDL_Surface *surf = SDL_LoadBMP_IO(io, true);
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

    const float scale = float(getHeight()) / float(logoH);
    dst.w = logoW * scale;

    Helpers::fillRect(renderer, dst, Colors::background);
    SDL_RenderTexture(renderer, logoTexture, nullptr, &dst);
}

void MenuBar::hideSubMenus()
{
    fileMenu->hideSubMenus();
    viewMenu->hideSubMenus();
    editMenu->hideSubMenus();
    optionsMenu->hideSubMenus();
    if (const auto window = getWindow())
    {
        if (const auto root = window->getRootComponent())
        {
            root->setDirty();
        }
    }
    openSubMenuOnMouseOver = false;
}

void MenuBar::resized()
{
    const float scale = 4.0f / state->pixelScale;

    const int fileW = int(40 * scale);
    const int viewW = int(40 * scale);
    const int editW = int(40 * scale); // wide enough for Undo/Redo text
    const int optionsW =
        int(60 * scale); // wide enough for Device Properties text
    const int h = getHeight();

    int logoSpace = 0;
    if (!logoData.empty())
    {
        const float aspect = logoH ? (float)logoW / (float)logoH : 1.0f;
        logoSpace = int(h * aspect);
    }

    fileMenu->setBounds(logoSpace, 0, fileW, h);
    viewMenu->setBounds(logoSpace + fileW, 0, viewW, h);
    editMenu->setBounds(logoSpace + fileW + viewW, 0, editW, h);
    optionsMenu->setBounds(logoSpace + fileW + viewW + editW, 0, optionsW, h);

    SDL_Rect backgroundBounds = getLocalBounds();
    backgroundBounds.x = optionsMenu->getBounds().x + optionsW;
    backgroundBounds.w = getWidth() - backgroundBounds.x;

    background->setBounds(backgroundBounds);
}

void MenuBar::mouseEnter()
{
    setDirty();
}

Menu *MenuBar::getOpenMenu() const
{
    if (fileMenu->isOpen())
    {
        return fileMenu;
    }
    if (viewMenu->isOpen())
    {
        return viewMenu;
    }
    if (editMenu->isOpen())
    {
        return editMenu;
    }
    if (optionsMenu->isOpen())
    {
        return optionsMenu;
    }
    return nullptr;
}

bool MenuBar::hasMenuOpen()
{
    return getOpenMenu() != nullptr;
}

bool MenuBar::mouseDown(const MouseEvent &)
{
    setOpenSubMenuOnMouseOver(false);
    return true;
}

void MenuBar::setOpenSubMenuOnMouseOver(
    const bool openSubMenuOnMouseOverEnabled)
{
    openSubMenuOnMouseOver = openSubMenuOnMouseOverEnabled;
}

bool MenuBar::shouldOpenSubMenuOnMouseOver() const
{
    return openSubMenuOnMouseOver;
}
