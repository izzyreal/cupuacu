#include "MenuBar.hpp"

#include "State.hpp"
#include "ResourceUtil.hpp"

#include "gui/OpaqueRect.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBarPlanning.hpp"
#include "gui/Window.hpp"
#include "gui/AmplifyFadeWindow.hpp"
#include "gui/DynamicsWindow.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/NormalizeWindow.hpp"
#include "gui/Colors.hpp"
#include "gui/UiScale.hpp"
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

    // File, Edit, View, Effects, Options menus
    fileMenu = emplaceChild<Menu>(state, "File");
    editMenu = emplaceChild<Menu>(state, "Edit");
    viewMenu = emplaceChild<Menu>(state, "View");
    effectsMenu = emplaceChild<Menu>(state, "Effects");
    optionsMenu = emplaceChild<Menu>(state, "Options");

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

    std::function<std::string()> undoMenuNameGetter = [&]
    {
        return buildUndoMenuLabel(state);
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
        return buildRedoMenuLabel(state);
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
            return isSelectionEditAvailable(state);
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
            return isSelectionEditAvailable(state);
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
            return isSelectionEditAvailable(state);
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
            return isPasteAvailable(state);
        });

    effectsMenu->addSubMenu(
        state, "Amplify/Fade",
        [&]
        {
            if (!state->amplifyFadeWindow || !state->amplifyFadeWindow->isOpen())
            {
                state->amplifyFadeWindow.reset(new AmplifyFadeWindow(state));
            }
            else
            {
                state->amplifyFadeWindow->raise();
            }
        });
    effectsMenu->addSubMenu(
        state, "Normalize",
        [&]
        {
            if (!state->normalizeWindow || !state->normalizeWindow->isOpen())
            {
                state->normalizeWindow.reset(new NormalizeWindow(state));
            }
            else
            {
                state->normalizeWindow->raise();
            }
        });
    effectsMenu->addSubMenu(
        state, "Dynamics",
        [&]
        {
            if (!state->dynamicsWindow || !state->dynamicsWindow->isOpen())
            {
                state->dynamicsWindow.reset(new DynamicsWindow(state));
            }
            else
            {
                state->dynamicsWindow->raise();
            }
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
    editMenu->hideSubMenus();
    viewMenu->hideSubMenus();
    effectsMenu->hideSubMenus();
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
    const float scale = getCanvasSpaceScale(state) * 4.0f;

    const int fileW = int(40 * scale);
    const int editW = int(40 * scale); // wide enough for Undo/Redo text
    const int viewW = int(40 * scale);
    const int effectsW = int(56 * scale);
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
    editMenu->setBounds(logoSpace + fileW, 0, editW, h);
    viewMenu->setBounds(logoSpace + fileW + editW, 0, viewW, h);
    effectsMenu->setBounds(logoSpace + fileW + editW + viewW, 0, effectsW, h);
    optionsMenu->setBounds(logoSpace + fileW + editW + viewW + effectsW, 0,
                           optionsW, h);

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
    if (editMenu->isOpen())
    {
        return editMenu;
    }
    if (viewMenu->isOpen())
    {
        return viewMenu;
    }
    if (effectsMenu->isOpen())
    {
        return effectsMenu;
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
