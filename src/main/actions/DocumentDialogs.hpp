#pragma once

#include "../State.hpp"
#include "../gui/ExportAudioDialogWindow.hpp"
#include "../gui/NewFileDialogWindow.hpp"

#include <SDL3/SDL.h>

namespace cupuacu::actions
{
    inline void showNewFileDialog(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        if (!state->newFileDialogWindow || !state->newFileDialogWindow->isOpen())
        {
            state->newFileDialogWindow.reset(new gui::NewFileDialogWindow(state));
        }
        else
        {
            state->newFileDialogWindow->raise();
        }
    }

    inline void showExportAudioDialog(
        cupuacu::State *state,
        const cupuacu::PendingSaveAsMode mode =
            cupuacu::PendingSaveAsMode::Generic)
    {
        if (!state)
        {
            return;
        }

        state->pendingSaveAsMode = mode;

        if (!state->exportAudioDialogWindow ||
            !state->exportAudioDialogWindow->isOpen())
        {
            state->exportAudioDialogWindow.reset(
                new gui::ExportAudioDialogWindow(state));
        }
        else
        {
            state->exportAudioDialogWindow->raise();
        }
    }

    inline void requestExit(cupuacu::State *state)
    {
        (void)state;
        SDL_Event quitEvent{};
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    }
} // namespace cupuacu::actions
