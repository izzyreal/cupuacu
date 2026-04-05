#include "MenuBar.hpp"

#include "State.hpp"
#include "ResourceUtil.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/DocumentTabs.hpp"
#include "effects/AmplifyEnvelopeEffect.hpp"
#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"
#include "effects/ReverseEffect.hpp"

#include "gui/OpaqueRect.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBarPlanning.hpp"
#include "gui/Window.hpp"
#include "gui/OptionsWindow.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"
#include "gui/NewFileDialogWindow.hpp"
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

#include <filesystem>
#include <memory>

using namespace cupuacu::gui;

namespace
{
    std::pair<int, int> loadLogoDimensions(const std::string &logoData)
    {
        if (logoData.empty())
        {
            return {0, 0};
        }

        SDL_IOStream *io = SDL_IOFromConstMem(
            logoData.data(), static_cast<int>(logoData.size()));
        if (!io)
        {
            return {0, 0};
        }

        SDL_Surface *surf = SDL_LoadBMP_IO(io, true);
        if (!surf)
        {
            return {0, 0};
        }

        const int w = surf->w;
        const int h = surf->h;
        SDL_DestroySurface(surf);
        return {w, h};
    }
} // namespace

MenuBar::MenuBar(State *stateToUse) : Component(stateToUse, "MenuBar")
{
    background = emplaceChild<OpaqueRect>(state, Colors::background);
    disableParentClipping();

    fileMenu = emplaceChild<Menu>(state, "File");
    editMenu = emplaceChild<Menu>(state, "Edit");
    viewMenu = emplaceChild<Menu>(state, "View");
    generateMenu = emplaceChild<Menu>(state, "Generate");
    effectsMenu = emplaceChild<Menu>(state, "Effects");
    optionsMenu = emplaceChild<Menu>(state, "Options");

#ifdef __APPLE__
    const std::string newText{"New file (Cmd + N)"};
    const std::string openText{"Open (Cmd + O)"};
    const std::string saveAsText{"Save as (Cmd + Shift + S)"};
    const std::string closeText{"Close file (Cmd + W)"};
    const std::string overwriteText{"Overwrite (Cmd + S)"};
    const std::string exitText{"Exit"};
    const std::string trimText{"Trim (Cmd + T)"};
    const std::string cutText{"Cut (Cmd + X)"};
    const std::string copyText{"Copy (Cmd + C)"};
    const std::string pasteText{"Paste (Cmd + V)"};
    const std::string allOptionsText{"All options (Cmd + ,)"};
#else
    const std::string newText{"New file (Ctrl + N)"};
    const std::string openText{"Open (Ctrl + O)"};
    const std::string saveAsText{"Save as (Ctrl + Shift + S)"};
    const std::string closeText{"Close file (Ctrl + W)"};
    const std::string overwriteText{"Overwrite (Ctrl + S)"};
    const std::string exitText{"Exit"};
    const std::string trimText{"Trim (Ctrl + T)"};
    const std::string cutText{"Cut (Ctrl + X)"};
    const std::string copyText{"Copy (Ctrl + C)"};
    const std::string pasteText{"Paste (Ctrl + V)"};
    const std::string allOptionsText{"All options (Ctrl + ,)"};
#endif

    fileMenu->addSubMenu(state, newText,
                         [&]
                         {
                             actions::showNewFileDialog(state);
                         });
    fileMenu->addSubMenu(state, openText,
                         [&]
                         {
                             actions::showOpenFileDialog(state);
                         });
    auto *saveAsMenu =
        fileMenu->addSubMenu(state, saveAsText,
                             [&]
                             {
                                 actions::showExportAudioDialog(state);
                             });
    saveAsMenu->setIsAvailable(
        [&]
        {
            return actions::hasActiveDocument(state);
        });

    auto *recentMenu = fileMenu->addSubMenu(state, "Recent");
    auto *emptyRecentMenu = recentMenu->addSubMenu(
        state,
        [&]() -> std::string
        {
            return state->recentFiles.empty() ? "No recent files" : "";
        },
        [] {});
    emptyRecentMenu->setIsAvailable(
        [&]
        {
            return state->recentFiles.empty();
        });
    for (std::size_t index = 0;
         index < persistence::RecentFilesPersistence::kMaxEntries; ++index)
    {
        auto *entry = recentMenu->addSubMenu(
            state,
            [this, index]() -> std::string
            {
                if (index >= state->recentFiles.size())
                {
                    return "";
                }
                return state->recentFiles[index];
            },
            [this, index]()
            {
                if (index >= state->recentFiles.size())
                {
                    return;
                }

                const auto path = state->recentFiles[index];
                if (!std::filesystem::exists(path))
                {
                    actions::removeRecentFile(state, path);
                    return;
                }

                actions::loadFileIntoNewTab(state, path);
            });
        entry->setIsAvailable(
            [this, index]()
            {
                return index < state->recentFiles.size();
            });
    }

    auto *closeMenu = fileMenu->addSubMenu(state, closeText,
                                           [&]
                                           {
                                               actions::closeActiveTab(state);
                                           });
    closeMenu->setIsAvailable(
        [&]
        {
            return actions::hasActiveDocument(state);
        });
    auto *overwriteMenu = fileMenu->addSubMenu(state, overwriteText,
                                               [&]
                                               {
                                                   actions::overwrite(state);
                                               });
    overwriteMenu->setIsAvailable(
        [&]
        {
            return actions::hasActiveDocument(state) &&
                   !state->getActiveDocumentSession().currentFile.empty();
        });
    fileMenu->addSubMenu(state, exitText,
                         [&]
                         {
                             actions::requestExit(state);
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

    generateMenu->addSubMenu(
        state, "Silence",
        [&]
        {
            if (!state->generateSilenceDialogWindow ||
                !state->generateSilenceDialogWindow->isOpen())
            {
                state->generateSilenceDialogWindow.reset(
                    new GenerateSilenceDialogWindow(state));
            }
            else
            {
                state->generateSilenceDialogWindow->raise();
            }
        });
    generateMenu->setIsAvailable(
        [&]
        {
            return actions::hasActiveDocument(state);
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
    const auto [loadedLogoW, loadedLogoH] = loadLogoDimensions(logoData);
    logoW = loadedLogoW;
    logoH = loadedLogoH;

    auto trimMenu = editMenu->addSubMenu(state, trimText,
                                         [&]
                                         {
                                             actions::audio::performTrim(state);
                                         });
    trimMenu->setIsAvailable(
        [&]
        {
            return isSelectionEditAvailable(state);
        });

    auto cutMenu = editMenu->addSubMenu(state, cutText,
                                        [&]
                                        {
                                            actions::audio::performCut(state);
                                        });
    cutMenu->setIsAvailable(
        [&]
        {
            return isSelectionEditAvailable(state);
        });

    auto copyMenu = editMenu->addSubMenu(state, copyText,
                                         [&]
                                         {
                                             actions::audio::performCopy(state);
                                         });
    copyMenu->setIsAvailable(
        [&]
        {
            return isSelectionEditAvailable(state);
        });

    auto pasteMenu =
        editMenu->addSubMenu(state, pasteText,
                             [&]
                             {
                                 actions::audio::performPaste(state);
                             });
    pasteMenu->setIsAvailable(
        [&]
        {
            return isPasteAvailable(state);
        });

    effectsMenu->addSubMenu(state, "Reverse",
                            [&]
                            {
                                effects::performReverse(state);
                            });
    effectsMenu->addSubMenu(state, "Amplify/Fade",
                            [&]
                            {
                                if (!state->amplifyFadeDialog ||
                                    !state->amplifyFadeDialog->isOpen())
                                {
                                    state->amplifyFadeDialog.reset(
                                        new effects::AmplifyFadeDialog(state));
                                }
                                else
                                {
                                    state->amplifyFadeDialog->raise();
                                }
                            });
    effectsMenu->addSubMenu(
        state, "Amplify Envelope",
        [&]
        {
            if (!state->amplifyEnvelopeDialog ||
                !state->amplifyEnvelopeDialog->isOpen())
            {
                state->amplifyEnvelopeDialog.reset(
                    new effects::AmplifyEnvelopeDialog(state));
            }
            else
            {
                state->amplifyEnvelopeDialog->raise();
            }
        });
    effectsMenu->addSubMenu(
        state, "Dynamics",
        [&]
        {
            if (!state->dynamicsDialog || !state->dynamicsDialog->isOpen())
            {
                state->dynamicsDialog.reset(new effects::DynamicsDialog(state));
            }
            else
            {
                state->dynamicsDialog->raise();
            }
        });
    effectsMenu->addSubMenu(
        state, "Remove silence",
        [&]
        {
            if (!state->removeSilenceDialog ||
                !state->removeSilenceDialog->isOpen())
            {
                state->removeSilenceDialog.reset(
                    new effects::RemoveSilenceDialog(state));
            }
            else
            {
                state->removeSilenceDialog->raise();
            }
        });
    effectsMenu->setIsAvailable(
        [&]
        {
            return actions::hasActiveDocument(state);
        });

    optionsMenu->addSubMenu(
        state, allOptionsText,
        [&]
        {
            showOptionsWindow(state);
        });
    optionsMenu->addSubMenu(
        state, "Audio",
        [&]
        {
            showOptionsWindow(state, OptionsSection::Audio);
        });
    optionsMenu->addSubMenu(
        state, "Display",
        [&]
        {
            showOptionsWindow(state, OptionsSection::Display);
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
    generateMenu->hideSubMenus();
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
    const int generateW = int(66 * scale);
    const int effectsW = int(56 * scale);
    const int optionsW =
        int(60 * scale); // wide enough for All options, Audio, and Display
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
    generateMenu->setBounds(logoSpace + fileW + editW + viewW, 0, generateW, h);
    effectsMenu->setBounds(logoSpace + fileW + editW + viewW + generateW, 0,
                           effectsW, h);
    optionsMenu->setBounds(logoSpace + fileW + editW + viewW + generateW +
                               effectsW,
                           0, optionsW, h);

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
    if (generateMenu->isOpen())
    {
        return generateMenu;
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
