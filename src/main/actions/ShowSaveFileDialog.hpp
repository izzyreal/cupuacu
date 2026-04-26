#pragma once
#include <SDL3/SDL.h>

#include "../file/AudioExport.hpp"
#include "PlatformSaveFileDialog.hpp"
#include "Save.hpp"

#include "../State.hpp"

#include <filesystem>
#include <string>

namespace cupuacu::actions
{
    namespace detail
    {
        inline std::string defaultSaveLocation(
            cupuacu::State *state, const file::AudioExportSettings &settings)
        {
            if (!state)
            {
                return {};
            }

            const auto &session = state->getActiveDocumentSession();
            if (!session.currentFile.empty())
            {
                return file::normalizeExportPath(session.currentFile, settings)
                    .string();
            }
            if (!session.preservationReferenceFile.empty())
            {
                return file::normalizeExportPath(session.preservationReferenceFile,
                                                 settings)
                    .string();
            }

            const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
            if (!home)
            {
                return {};
            }

            auto path = std::filesystem::path(home) / "Untitled";
            return file::normalizeExportPath(path, settings).string();
        }
    } // namespace detail

    static SDL_Window *getSaveFileDialogParentWindow(cupuacu::State *state)
    {
        if (!state)
        {
            return nullptr;
        }
        if (state->mainDocumentSessionWindow &&
            state->mainDocumentSessionWindow->getWindow() &&
            state->mainDocumentSessionWindow->getWindow()->isOpen())
        {
            return state->mainDocumentSessionWindow->getWindow()->getSdlWindow();
        }
        return nullptr;
    }

    static void saveFileDialogCallback(void *userdata,
                                       const char *const *filelist, int)
    {
        auto *state = static_cast<cupuacu::State *>(userdata);
        const auto settings =
            state ? state->pendingSaveAsExportSettings : std::nullopt;
        const auto mode =
            state ? state->pendingSaveAsMode : cupuacu::PendingSaveAsMode::Generic;
        if (state)
        {
            state->pendingSaveAsExportSettings.reset();
            state->pendingSaveAsMode = cupuacu::PendingSaveAsMode::Generic;
        }

        if (!filelist)
        {
            if (state)
            {
                state->pendingSaveAsMarkerWarningConfirmed = false;
            }
            SDL_Log("An error occured: %s", SDL_GetError());
            return;
        }
        else if (!*filelist)
        {
            if (state)
            {
                state->pendingSaveAsMarkerWarningConfirmed = false;
            }
            SDL_Log("The user did not select any file.");
            SDL_Log("Most likely, the dialog was canceled.");
            return;
        }

        if (!state || !settings.has_value())
        {
            if (state)
            {
                state->pendingSaveAsMarkerWarningConfirmed = false;
            }
            return;
        }

        if (mode == cupuacu::PendingSaveAsMode::Preserving)
        {
            actions::saveAsPreserving(state, *filelist, *settings);
            if (state)
            {
                state->pendingSaveAsMarkerWarningConfirmed = false;
            }
            return;
        }

        actions::saveAs(state, *filelist, *settings);
        if (state)
        {
            state->pendingSaveAsMarkerWarningConfirmed = false;
        }
    }

    static void showSaveFileDialog(cupuacu::State *state,
                                   const file::AudioExportSettings &settings)
    {
        if (!state || !settings.isValid())
        {
            return;
        }

        if (!detail::confirmMarkerPersistenceIfNeeded(state, settings))
        {
            return;
        }

        state->pendingSaveAsExportSettings = settings;
        state->pendingSaveAsMarkerWarningConfirmed = true;
        const auto defaultLocation =
            detail::defaultSaveLocation(state, settings);
        if (showPlatformSaveFileDialog(saveFileDialogCallback, state,
                                       getSaveFileDialogParentWindow(state),
                                       defaultLocation))
        {
            return;
        }

        SDL_ShowSaveFileDialog(
            saveFileDialogCallback, state, getSaveFileDialogParentWindow(state),
            nullptr, 0,
            defaultLocation.empty() ? nullptr : defaultLocation.c_str());
    }
} // namespace cupuacu::actions
