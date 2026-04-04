#pragma once
#include <SDL3/SDL.h>
#include "../State.hpp"
#include "Gui.hpp"
#include "Component.hpp"
#include "DropdownMenu.hpp"
#include "MenuBar.hpp"
#include "Window.hpp"
#include "MouseEvent.hpp"
#include "Waveform.hpp"
#include "TriangleMarker.hpp"
#include "keyboard_handling.hpp"
#include "../actions/DocumentTabs.hpp"
#include "../ResourceUtil.hpp"

namespace cupuacu::gui
{
    struct CursorSet
    {
        SDL_Cursor *defaultCursor = nullptr;
        SDL_Cursor *textCursor = nullptr;
        SDL_Cursor *pointerCursor = nullptr;
        SDL_Cursor *currentCursor = nullptr;
        SDL_Cursor *selectLCursor = nullptr;
        SDL_Cursor *selectRCursor = nullptr;
    };

    static CursorSet &getCursorSet()
    {
        static CursorSet cursors;
        return cursors;
    }

    static void cleanupCursors();

    static SDL_Cursor *loadCustomCursor(const std::string &filename,
                                        const int hot_x, const int hot_y)
    {
        const auto data = get_resource_data(filename);
        if (data.empty())
        {
            SDL_Log("Cursor resource '%s' not found", filename.c_str());
            return nullptr;
        }

        SDL_IOStream *io = SDL_IOFromConstMem(data.data(), (int)data.size());
        SDL_Surface *surface = SDL_LoadBMP_IO(io, 1);
        if (!surface)
        {
            SDL_Log("SDL_LoadBMP_IO failed: %s", SDL_GetError());
            return nullptr;
        }

        SDL_Cursor *cursor = SDL_CreateColorCursor(surface, hot_x, hot_y);
        SDL_DestroySurface(surface);

        if (!cursor)
        {
            SDL_Log("SDL_CreateColorCursor failed: %s", SDL_GetError());
        }
        return cursor;
    }

    static void initCursors()
    {
        auto &cursors = getCursorSet();
        cleanupCursors();

        cursors.defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        cursors.textCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
        cursors.pointerCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        cursors.currentCursor = cursors.defaultCursor;

        cursors.selectLCursor = loadCustomCursor("select_l.bmp", 0, 0);
        cursors.selectRCursor = loadCustomCursor("select_r.bmp", 0, 0);

        SDL_SetCursor(cursors.currentCursor);
    }

    static void cleanupCursors()
    {
        auto &cursors = getCursorSet();
        if (cursors.defaultCursor)
        {
            SDL_DestroyCursor(cursors.defaultCursor);
            cursors.defaultCursor = nullptr;
        }
        if (cursors.textCursor)
        {
            SDL_DestroyCursor(cursors.textCursor);
            cursors.textCursor = nullptr;
        }
        if (cursors.pointerCursor)
        {
            SDL_DestroyCursor(cursors.pointerCursor);
            cursors.pointerCursor = nullptr;
        }
        if (cursors.selectLCursor)
        {
            SDL_DestroyCursor(cursors.selectLCursor);
            cursors.selectLCursor = nullptr;
        }
        if (cursors.selectRCursor)
        {
            SDL_DestroyCursor(cursors.selectRCursor);
            cursors.selectRCursor = nullptr;
        }
        cursors.currentCursor = nullptr;
    }

    static void updateMouseCursor(const State *state, const Window *window)
    {
        if (!window)
        {
            return;
        }
        auto &cursors = getCursorSet();
        const auto &viewState =
            state->getActiveViewState();

        SDL_Cursor *newCursor = cursors.defaultCursor;
        const SDL_Window *focus = SDL_GetKeyboardFocus();
        if (!focus || focus != window->getSdlWindow())
        {
            if (newCursor != cursors.currentCursor)
            {
                SDL_SetCursor(newCursor);
                cursors.currentCursor = newCursor;
            }
            return;
        }

        if (window->getMenuBar() && window->getMenuBar()->hasMenuOpen())
        {
            newCursor = cursors.defaultCursor;
        }
        else if (dynamic_cast<const Waveform *>(
                     window->getComponentUnderMouse()))
        {
            if (viewState.hoveringOverChannels == LEFT)
            {
                newCursor = cursors.selectLCursor;
            }
            else if (viewState.hoveringOverChannels == RIGHT)
            {
                newCursor = cursors.selectRCursor;
            }
            else /* SelectedChannels::BOTH */
            {
                newCursor = cursors.textCursor;
            }
        }
        else if (dynamic_cast<const SamplePoint *>(
                     window->getComponentUnderMouse()) ||
                 dynamic_cast<const TriangleMarker *>(
                     window->getComponentUnderMouse()))
        {
            newCursor = cursors.pointerCursor;
        }

        if (newCursor != cursors.currentCursor)
        {
            SDL_SetCursor(newCursor);
            cursors.currentCursor = newCursor;
        }
    }

    static void handleWindowMouseLeave(State *state, Window *window)
    {
        if (!window)
        {
            return;
        }

        window->hideTooltip();

        if (window->getCapturingComponent() != nullptr)
        {
            return;
        }

        if (state->getActiveDocumentSession().selection.isActive())
        {
            return;
        }

        for (auto *waveform : state->waveforms)
        {
            waveform->clearHighlight();
        }

        window->setComponentUnderMouse(nullptr);
    }

    static SDL_WindowID getEventWindowId(const SDL_Event *event)
    {
        switch (event->type)
        {
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                return event->window.windowID;
            case SDL_EVENT_MOUSE_MOTION:
                return event->motion.windowID;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                return event->button.windowID;
            case SDL_EVENT_MOUSE_WHEEL:
                return event->wheel.windowID;
            case SDL_EVENT_KEY_DOWN:
                return event->key.windowID;
            case SDL_EVENT_TEXT_INPUT:
                return event->text.windowID;
            default:
                return 0;
        }
    }

    static Window *findWindowForEvent(State *state, const SDL_Event *event)
    {
        auto findById = [state](const SDL_WindowID windowId) -> Window *
        {
            if (windowId == 0)
            {
                return nullptr;
            }

            for (auto *window : state->windows)
            {
                if (window && window->getId() == windowId)
                {
                    return window;
                }
            }
            return nullptr;
        };

        const SDL_WindowID windowId = getEventWindowId(event);
        if (auto *window = findById(windowId))
        {
            return window;
        }

        switch (event->type)
        {
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_WHEEL:
                if (const SDL_Window *mouseFocus = SDL_GetMouseFocus())
                {
                    return findById(SDL_GetWindowID(
                        const_cast<SDL_Window *>(mouseFocus)));
                }
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_TEXT_INPUT:
                if (const SDL_Window *keyboardFocus = SDL_GetKeyboardFocus())
                {
                    return findById(SDL_GetWindowID(
                        const_cast<SDL_Window *>(keyboardFocus)));
                }
                break;
            default:
                break;
        }

        return nullptr;
    }

    static bool isInteractiveEventBlockedByModal(State *state,
                                                 Window *eventWindow,
                                                 const SDL_Event *event)
    {
        if (!state || !state->modalWindow || !state->modalWindow->isOpen())
        {
            return false;
        }

        if (eventWindow == state->modalWindow)
        {
            return false;
        }

        if (eventWindow && eventWindow->getRootComponent())
        {
            if (const auto *dropdownOwner =
                    dynamic_cast<const DropdownOwnerComponent *>(
                        eventWindow->getRootComponent()))
            {
                if (auto *owningDropdown = dropdownOwner->getOwningDropdown())
                {
                    if (owningDropdown->getWindow() == state->modalWindow)
                    {
                        return false;
                    }
                }
            }
        }

        switch (event->type)
        {
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_WHEEL:
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_TEXT_INPUT:
                if (state->modalWindow->getSdlWindow())
                {
                    SDL_RaiseWindow(state->modalWindow->getSdlWindow());
                }
                return true;
            default:
                return false;
        }
    }

    inline SDL_AppResult handleAppEvent(State *state, SDL_Event *event)
    {
        auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
        Window *eventWindow = findWindowForEvent(state, event);
        if (isInteractiveEventBlockedByModal(state, eventWindow, event))
        {
            return SDL_APP_CONTINUE;
        }
        switch (event->type)
        {
            case SDL_EVENT_QUIT:
                cleanupCursors();
                return SDL_APP_SUCCESS;
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESIZED:
                if (eventWindow)
                {
                    eventWindow->handleEvent(*event);
                }
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                if (eventWindow)
                {
                    if (mainWindow && eventWindow == mainWindow)
                    {
                        handleWindowMouseLeave(state, eventWindow);
                    }
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (eventWindow && eventWindow->hasFocus())
                {
                    if (mainWindow && eventWindow == mainWindow &&
                        !eventWindow->hasFocusedComponent())
                    {
                        handleKeyDown(event, state);
                    }
                    else
                    {
                        eventWindow->handleEvent(*event);
                    }
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (eventWindow && eventWindow->hasFocus())
                {
                    eventWindow->handleEvent(*event);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (eventWindow)
                {
                    eventWindow->handleEvent(*event);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (eventWindow)
                {
                    eventWindow->handleEvent(*event);
                }
                // state->rootComponent->printTree();
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (eventWindow)
                {
                    eventWindow->handleEvent(*event);
                }
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (eventWindow)
                {
                    if (mainWindow && eventWindow == mainWindow)
                    {
                        return SDL_APP_SUCCESS;
                    }
                    else
                    {
                        eventWindow->handleEvent(*event);
                    }
                }
                break;
            default:
                if (eventWindow)
                {
                    eventWindow->handleEvent(*event);
                }
                break;
        }

        if (mainWindow)
        {
            updateMouseCursor(state, mainWindow);
        }

        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            // state->getActiveDocumentSession().selection.printInfo();
        }

        return SDL_APP_CONTINUE;
    }
} // namespace cupuacu::gui
