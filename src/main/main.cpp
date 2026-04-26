#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "State.hpp"
#include "Logger.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Gui.hpp"
#include "gui/OptionsWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"
#include "gui/NewFileDialogWindow.hpp"
#include "gui/WindowPlacementPlanning.hpp"
#include "gui/UiScale.hpp"
#include "gui/Window.hpp"
#include "actions/BackgroundOpen.hpp"
#include "actions/DocumentLifecycle.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "audio/AudioDevices.hpp"
#include "persistence/AudioDevicePropertiesPersistence.hpp"
#include "persistence/DisplayPropertiesPersistence.hpp"
#include "persistence/RecentFilesPersistence.hpp"
#include "persistence/SessionStatePersistence.hpp"

#if defined(__APPLE__)
#include "platform/macos/MenuAdjustments.hpp"
#endif

#if CUPUACU_RTSAN_LIBS_ENABLED
#include <rtsan_standalone/rtsan_standalone.h>
#endif

namespace
{
    constexpr int kDefaultMainWindowWidth = 1280;
    constexpr int kDefaultMainWindowHeight = 720;

    constexpr Uint32 getHighDensityWindowFlag()
    {
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }

    SDL_Point resolveInitialMainWindowSize(
        const cupuacu::persistence::PersistedSessionState &persistedSessionState)
    {
        const bool hasPersistedSize =
            persistedSessionState.windowWidth.has_value() &&
            persistedSessionState.windowHeight.has_value() &&
            *persistedSessionState.windowWidth > 0 &&
            *persistedSessionState.windowHeight > 0;

        if (hasPersistedSize)
        {
            return {
                *persistedSessionState.windowWidth,
                *persistedSessionState.windowHeight,
            };
        }

        return {kDefaultMainWindowWidth, kDefaultMainWindowHeight};
    }

    std::vector<SDL_Rect> getDisplayUsableBounds()
    {
        std::vector<SDL_Rect> result;
        int displayCount = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
        if (!displays || displayCount <= 0)
        {
            return result;
        }

        for (int index = 0; index < displayCount; ++index)
        {
            SDL_Rect bounds{};
            if (SDL_GetDisplayUsableBounds(displays[index], &bounds))
            {
                result.push_back(bounds);
            }
        }

        SDL_free(displays);
        return result;
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
#if CUPUACU_RTSAN_LIBS_ENABLED
    __rtsan::Initialize();
#endif

    cupuacu::State *state = new cupuacu::State();
    auto &session = state->getActiveDocumentSession();
    cupuacu::logging::initialize(state->paths.get());
    cupuacu::logging::info("Cupuacu starting");

    state->audioDevices = std::make_shared<cupuacu::audio::AudioDevices>();
    if (const auto persistedDisplayProperties =
            cupuacu::persistence::DisplayPropertiesPersistence::load(
                state->paths->displayPropertiesPath());
        persistedDisplayProperties.has_value())
    {
        state->pixelScale = persistedDisplayProperties->pixelScale;
        state->vuMeterScale = persistedDisplayProperties->vuMeterScale;
    }
    if (const auto persistedSelection =
            cupuacu::persistence::AudioDevicePropertiesPersistence::load(
                state->paths->audioDevicePropertiesPath());
        persistedSelection.has_value())
    {
        state->audioDevices->setDeviceSelection(*persistedSelection);
    }
    const auto persistedRecentFiles =
        cupuacu::persistence::RecentFilesPersistence::load(
            state->paths->recentlyOpenedFilesPath());
    state->recentFiles = persistedRecentFiles;
    const auto persistedSessionState =
        cupuacu::persistence::SessionStatePersistence::load(
            state->paths->sessionStatePath());
    const SDL_Point initialMainWindowSize =
        resolveInitialMainWindowSize(persistedSessionState);

    *appstate = state;

    SDL_SetAppMetadata("Cupuacu -- A minimalist audio editor by Izmar", "0.1",
                       "nl.izmar.cupuacu");

    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        cupuacu::logging::error("SDL video initialization failed");
        return SDL_APP_FAILURE;
    }

    if (!TTF_Init())
    {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        cupuacu::logging::error("SDL_ttf initialization failed");
        return SDL_APP_FAILURE;
    }

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    state->mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            state, &session, &state->getActiveViewState(), "",
            initialMainWindowSize.x, initialMainWindowSize.y,
            SDL_WINDOW_RESIZABLE | getHighDensityWindowFlag());

    auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
    if (!mainWindow || !mainWindow->isOpen())
    {
        return SDL_APP_FAILURE;
    }

    const auto initialPlacement = cupuacu::gui::planInitialWindowPlacement(
        persistedSessionState.windowX, persistedSessionState.windowY,
        initialMainWindowSize.x, initialMainWindowSize.y,
        getDisplayUsableBounds());
    if (initialPlacement.usePersistedPosition)
    {
        SDL_SetWindowPosition(mainWindow->getSdlWindow(), initialPlacement.x,
                              initialPlacement.y);
    }
    state->windows.push_back(mainWindow);

#if defined(__APPLE__)
    cupuacu::platform::macos::clearWindowCloseShortcut();
#endif

    resetWaveformState(state);
    resetSampleValueUnderMouseCursor(state);

    cupuacu::gui::initCursors();

    cupuacu::gui::buildComponents(state, mainWindow);

    cupuacu::actions::restoreStartupDocument(
        state, persistedRecentFiles, persistedSessionState, true);

    if (state->getActiveDocumentSession().currentFile.empty())
    {
        cupuacu::actions::resetZoom(state);
    }

    mainWindow->renderFrame();
    state->mainWindowInitialFrameRendered = true;
    if (state->pendingStartupWarning.has_value())
    {
        const auto [title, message] = *state->pendingStartupWarning;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title.c_str(),
                                 message.c_str(), mainWindow->getSdlWindow());
        state->pendingStartupWarning.reset();
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    cupuacu::State *state = (cupuacu::State *)appstate;
    cupuacu::actions::processPendingOpenWork(state);

    for (auto *window : state->windows)
    {
        if (window && window->isOpen() && window->getRootComponent())
        {
            window->getRootComponent()->timerCallbackRecursive();
        }
    }

    bool renderedAnyWindow = false;
    for (auto *window : state->windows)
    {
        if (window && window->isOpen())
        {
            window->updateTooltip();
            const bool hadDirty = !window->getDirtyRects().empty();
            window->renderFrameIfDirty();
            renderedAnyWindow = renderedAnyWindow || hadDirty;
        }
    }

    if (state->optionsWindow && !state->optionsWindow->isOpen())
    {
        state->optionsWindow.reset();
    }
    if (state->newFileDialogWindow && !state->newFileDialogWindow->isOpen())
    {
        state->newFileDialogWindow.reset();
    }
    if (state->generateSilenceDialogWindow &&
        !state->generateSilenceDialogWindow->isOpen())
    {
        state->generateSilenceDialogWindow.reset();
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
    cupuacu::actions::persistSessionState(state);
    cupuacu::persistence::DisplayPropertiesPersistence::save(
        state->paths->displayPropertiesPath(),
        {.pixelScale = state->pixelScale, .vuMeterScale = state->vuMeterScale});
    state->generateSilenceDialogWindow.reset();
    state->backgroundOpenJob.reset();
    state->newFileDialogWindow.reset();
    state->optionsWindow.reset();
    state->mainDocumentSessionWindow.reset();
    state->windows.clear();
    SDL_Quit();
    cupuacu::logging::info("Cupuacu shutting down");
    cupuacu::logging::shutdown();
}
