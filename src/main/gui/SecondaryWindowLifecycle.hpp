#pragma once

#include "../State.hpp"

#include "Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace cupuacu::gui
{
    inline void attachSecondaryWindow(State *state, Window *window,
                                      const bool modal)
    {
        if (!state || !window)
        {
            return;
        }

        state->windows.push_back(window);
        if (modal)
        {
            state->modalWindow = window;
        }

        auto *mainWindow = state->mainDocumentSessionWindow
                               ? state->mainDocumentSessionWindow->getWindow()
                               : nullptr;
        if (mainWindow && mainWindow->getSdlWindow() && window->getSdlWindow())
        {
            SDL_SetWindowParent(window->getSdlWindow(), mainWindow->getSdlWindow());
        }
    }

    inline void detachSecondaryWindow(State *state, Window *window)
    {
        if (!state || !window)
        {
            return;
        }

        const auto it =
            std::find(state->windows.begin(), state->windows.end(), window);
        if (it != state->windows.end())
        {
            state->windows.erase(it);
        }
        if (state->modalWindow == window)
        {
            state->modalWindow = nullptr;
        }
    }

    inline void requestSecondaryWindowClose(State *state, Window *window)
    {
        if (!window || !window->isOpen())
        {
            detachSecondaryWindow(state, window);
            return;
        }

        window->requestClose();
    }

    inline void raiseSecondaryWindow(Window *window)
    {
        if (window && window->getSdlWindow())
        {
            SDL_RaiseWindow(window->getSdlWindow());
        }
    }
} // namespace cupuacu::gui
