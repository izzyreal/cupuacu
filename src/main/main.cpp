#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "State.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Gui.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

const uint16_t initialDimensions[] = {1280, 720};

#include <cstdint>
#include <string>
#include <filesystem>
#include <algorithm>

#include "file/file_loading.hpp"

#include "AudioDevices.hpp"

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    cupuacu::State *state = new cupuacu::State();

    state->audioDevices = std::make_shared<cupuacu::AudioDevices>();

    resetWaveformState(state);
    resetSampleValueUnderMouseCursor(state);

    *appstate = state;

    SDL_SetAppMetadata("Cupuacu -- A minimalist audio editor by Izmar", "0.1",
                       "nl.izmar.cupuacu");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!TTF_Init())
    {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    state->mainWindow = std::make_unique<cupuacu::gui::Window>(
        state, "", initialDimensions[0], initialDimensions[1],
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!state->mainWindow->isOpen())
    {
        return SDL_APP_FAILURE;
    }
    state->windows.push_back(state->mainWindow.get());

    cupuacu::gui::initCursors();

    if (std::filesystem::exists(state->currentFile))
    {
        cupuacu::file::loadSampleData(state);
        SDL_SetWindowTitle(state->mainWindow->getSdlWindow(),
                           state->currentFile.c_str());
    }
    else
    {
        state->currentFile = "";
    }

    cupuacu::gui::buildComponents(state, state->mainWindow.get());

    cupuacu::actions::resetZoom(state);

    state->mainWindow->renderFrame();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    cupuacu::State *state = (cupuacu::State *)appstate;

    if (state->mainWindow && state->mainWindow->getRootComponent())
    {
        state->mainWindow->getRootComponent()->timerCallbackRecursive();
    }

    for (auto *window : state->windows)
    {
        if (window && window->isOpen())
        {
            window->renderFrameIfDirty();
        }
    }

    if (state->devicePropertiesWindow &&
        !state->devicePropertiesWindow->isOpen())
    {
        state->devicePropertiesWindow.reset();
    }

    state->windows.erase(std::remove_if(state->windows.begin(),
                                        state->windows.end(),
                                        [](cupuacu::gui::Window *window)
                                        {
                                            return !window || !window->isOpen();
                                        }),
                         state->windows.end());

    SDL_Delay(16);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    return cupuacu::gui::handleAppEvent((cupuacu::State *)appstate, event);
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    TTF_Quit();
    cupuacu::State *state = (cupuacu::State *)appstate;
    state->devicePropertiesWindow.reset();
    state->mainWindow.reset();
    state->windows.clear();
    SDL_Quit();
}
