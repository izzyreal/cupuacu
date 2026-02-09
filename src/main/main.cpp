#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "State.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Gui.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/Window.hpp"

const uint16_t initialDimensions[] = {1280, 720};

#include <cstdint>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <algorithm>

#include "file/file_loading.hpp"

#include "audio/AudioDevices.hpp"
#include "persistence/AudioDevicePropertiesPersistence.hpp"

#if CUPUACU_RTSAN_LIBS_ENABLED
#include <rtsan_standalone/rtsan_standalone.h>
#endif

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
#if CUPUACU_RTSAN_LIBS_ENABLED
    __rtsan::Initialize();
#endif

    cupuacu::State *state = new cupuacu::State();
    auto &session = state->activeDocumentSession;

    state->audioDevices = std::make_shared<cupuacu::audio::AudioDevices>();
    if (const auto persistedSelection =
            cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                state->paths->audioDevicePropertiesPath());
        persistedSelection.has_value())
    {
        state->audioDevices->setDeviceSelection(*persistedSelection);
    }

    *appstate = state;

    SDL_SetAppMetadata("Cupuacu -- A minimalist audio editor by Izmar", "0.1",
                       "nl.izmar.cupuacu");

    if (const char *forcedRenderDriver = std::getenv("CUPUACU_RENDER_DRIVER");
        forcedRenderDriver && forcedRenderDriver[0] != '\0')
    {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, forcedRenderDriver);
        SDL_Log("Requested renderer driver: %s", forcedRenderDriver);
    }

    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");

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

    state->mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            state, &session, "", initialDimensions[0], initialDimensions[1],
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
    if (!mainWindow || !mainWindow->isOpen())
    {
        return SDL_APP_FAILURE;
    }
    state->windows.push_back(mainWindow);

    resetWaveformState(state);
    resetSampleValueUnderMouseCursor(state);

    cupuacu::gui::initCursors();

    if (std::filesystem::exists(session.currentFile))
    {
        cupuacu::file::loadSampleData(state);
        SDL_SetWindowTitle(mainWindow->getSdlWindow(),
                           session.currentFile.c_str());
    }
    else
    {
        session.currentFile = "";
    }

    cupuacu::gui::buildComponents(state, mainWindow);

    cupuacu::actions::resetZoom(state);

    mainWindow->renderFrame();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    cupuacu::State *state = (cupuacu::State *)appstate;
    auto *mainWindow = state->mainDocumentSessionWindow
                           ? state->mainDocumentSessionWindow->getWindow()
                           : nullptr;

    if (mainWindow && mainWindow->getRootComponent())
    {
        mainWindow->getRootComponent()->timerCallbackRecursive();
    }

    bool renderedAnyWindow = false;
    for (auto *window : state->windows)
    {
        if (window && window->isOpen())
        {
            const bool hadDirty = !window->getDirtyRects().empty();
            window->renderFrameIfDirty();
            renderedAnyWindow = renderedAnyWindow || hadDirty;
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

    if (!renderedAnyWindow)
    {
        SDL_Delay(1);
    }
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
    state->mainDocumentSessionWindow.reset();
    state->windows.clear();
    SDL_Quit();
}
