#pragma once
#include <SDL3/SDL.h>
#include "../State.h"
#include "Gui.h"
#include "Component.h"
#include "MenuBar.h"
#include "Window.h"
#include "MouseEvent.h"
#include "Waveform.h"
#include "TriangleMarker.h"
#include "keyboard_handling.h"
#include "../ResourceUtil.hpp"

namespace cupuacu::gui
{

    static SDL_Cursor *defaultCursor = nullptr;
    static SDL_Cursor *textCursor = nullptr;
    static SDL_Cursor *pointerCursor = nullptr;
    static SDL_Cursor *currentCursor = nullptr;
    static SDL_Cursor *selectLCursor = nullptr;
    static SDL_Cursor *selectRCursor = nullptr;

    static SDL_Cursor *loadCustomCursor(const std::string &filename, int hot_x,
                                        int hot_y)
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
        defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        textCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
        pointerCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        currentCursor = defaultCursor;

        selectLCursor = loadCustomCursor("select_l.bmp", 0, 0);
        selectRCursor = loadCustomCursor("select_r.bmp", 0, 0);

        SDL_SetCursor(currentCursor);
    }

    static void cleanupCursors()
    {
        if (defaultCursor)
        {
            SDL_DestroyCursor(defaultCursor);
        }
        if (textCursor)
        {
            SDL_DestroyCursor(textCursor);
        }
        if (pointerCursor)
        {
            SDL_DestroyCursor(pointerCursor);
        }
    }

    static void updateMouseCursor(const State *state,
                                  const Window *window)
    {
        if (!window)
        {
            return;
        }

        SDL_Cursor *newCursor = defaultCursor;
        const SDL_Window *focus = SDL_GetKeyboardFocus();
        if (!focus || focus != window->getSdlWindow())
        {
            if (newCursor != currentCursor)
            {
                SDL_SetCursor(newCursor);
                currentCursor = newCursor;
            }
            return;
        }

        if (window->getMenuBar() && window->getMenuBar()->hasMenuOpen())
        {
            newCursor = defaultCursor;
        }
        else if (dynamic_cast<const Waveform *>(
                     window->getComponentUnderMouse()))
        {
            if (state->hoveringOverChannels == LEFT)
            {
                newCursor = selectLCursor;
            }
            else if (state->hoveringOverChannels == RIGHT)
            {
                newCursor = selectRCursor;
            }
            else /* SelectedChannels::BOTH */
            {
                newCursor = textCursor;
            }
        }
        else if (dynamic_cast<const SamplePoint *>(
                     window->getComponentUnderMouse()) ||
                 dynamic_cast<const TriangleMarker *>(
                     window->getComponentUnderMouse()))
        {
            newCursor = pointerCursor;
        }

        if (newCursor != currentCursor)
        {
            SDL_SetCursor(newCursor);
            currentCursor = newCursor;
        }
    }

    static void handleWindowMouseLeave(State *state, Window *window)
    {
        if (!window)
        {
            return;
        }

        if (window->getCapturingComponent() != nullptr)
        {
            return;
        }

        if (state->selection.isActive())
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
            case SDL_EVENT_KEY_DOWN:
                return event->key.windowID;
            default:
                return 0;
        }
    }

    static Window *findWindowForEvent(State *state,
                                      const SDL_Event *event)
    {
        const SDL_WindowID windowId = getEventWindowId(event);
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
    }

    inline SDL_AppResult handleAppEvent(State *state, SDL_Event *event)
    {
        Window *eventWindow = findWindowForEvent(state, event);
        switch (event->type)
        {
            case SDL_EVENT_QUIT:
                cleanupCursors();
                return SDL_APP_SUCCESS;
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESIZED:
                if (eventWindow && eventWindow->hasFocus())
                {
                    eventWindow->handleEvent(*event);
                }
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                if (eventWindow && eventWindow->hasFocus())
                {
                    if (state->mainWindow &&
                        eventWindow == state->mainWindow.get())
                    {
                        handleWindowMouseLeave(state, eventWindow);
                    }
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (eventWindow && eventWindow->hasFocus() &&
                    state->mainWindow &&
                    eventWindow == state->mainWindow.get())
                {
                    handleKeyDown(event, state);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (eventWindow && eventWindow->hasFocus())
                {
                    eventWindow->handleEvent(*event);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (eventWindow && eventWindow->hasFocus())
                {
                    eventWindow->handleEvent(*event);
                }
                // state->rootComponent->printTree();
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (eventWindow && eventWindow->hasFocus())
                {
                    eventWindow->handleEvent(*event);
                }
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (eventWindow && eventWindow->hasFocus())
                {
                    eventWindow->handleEvent(*event);
                    if (state->mainWindow &&
                        eventWindow == state->mainWindow.get())
                    {
                        cleanupCursors();
                        return SDL_APP_SUCCESS;
                    }
                }
                break;
            default:
                if (eventWindow && eventWindow->hasFocus())
                {
                    eventWindow->handleEvent(*event);
                }
                break;
        }

        if (state->mainWindow)
        {
            updateMouseCursor(state, state->mainWindow.get());
        }

        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            // state->selection.printInfo();
        }

        return SDL_APP_CONTINUE;
    }
} // namespace cupuacu::gui
