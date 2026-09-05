#pragma once

#include "State.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "actions/io/BackgroundOpen.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "gui/EventHandling.hpp"
#include "gui/OptionsWindow.hpp"
#include "gui/NewFileDialogWindow.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"

namespace cupuacu
{
    // Shared by the SDL callback and the benchmark event driver.
    inline SDL_AppResult iterateApplication(State *state)
    {
        cupuacu::actions::effects::processPendingEffectWork(state);
        cupuacu::actions::io::processPendingOpenWork(state);
        cupuacu::actions::io::processPendingSaveWork(state);
        cupuacu::actions::io::processPendingAutosaveWork(state);

        if (state->quitRequestedAfterLongTaskCancel &&
            !state->backgroundOpenJob &&
            !state->pendingOpenWaveformBuild.active && !state->longTask.active)
        {
            cupuacu::gui::cleanupCursors();
            return SDL_APP_SUCCESS;
        }

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
        state->windows.erase(
            std::remove_if(state->windows.begin(), state->windows.end(),
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
} // namespace cupuacu
