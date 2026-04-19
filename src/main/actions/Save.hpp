#pragma once

#include "../State.hpp"
#include "../file/AudioExport.hpp"
#include "../file/AudioFileWriter.hpp"
#include "../file/OverwritePreservation.hpp"
#include "../file/PreservationBackend.hpp"
#include "../file/SaveWritePlan.hpp"
#include "DocumentLifecycle.hpp"

#include <SDL3/SDL.h>

#include <exception>
#include <filesystem>
#include <string>

namespace cupuacu::actions
{
    namespace detail
    {
        static SDL_Window *getSaveErrorParentWindow(cupuacu::State *state)
        {
            if (!state)
            {
                return nullptr;
            }
            if (state->modalWindow && state->modalWindow->isOpen())
            {
                return state->modalWindow->getSdlWindow();
            }
            if (state->mainDocumentSessionWindow &&
                state->mainDocumentSessionWindow->getWindow() &&
                state->mainDocumentSessionWindow->getWindow()->isOpen())
            {
                return state->mainDocumentSessionWindow->getWindow()->getSdlWindow();
            }
            return nullptr;
        }

        static void reportSaveFailure(cupuacu::State *state,
                                      const char *operation,
                                      const std::string &path,
                                      const std::string &reason)
        {
            std::string message = std::string(operation) + " failed";
            if (!path.empty())
            {
                message += " for:\n" + path;
            }
            if (!reason.empty())
            {
                message += "\n\n" + reason;
            }

            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
            if (state && state->errorReporter)
            {
                state->errorReporter("Save failed", message);
                return;
            }
            if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
            {
                return;
            }

            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save failed",
                                     message.c_str(),
                                     getSaveErrorParentWindow(state));
        }

        template <typename Fn>
        static bool runSaveOperation(cupuacu::State *state,
                                     const char *operation,
                                     const std::string &path, Fn &&fn)
        {
            try
            {
                fn();
                return true;
            }
            catch (const std::exception &e)
            {
                reportSaveFailure(state, operation, path, e.what());
                return false;
            }
            catch (...)
            {
                reportSaveFailure(state, operation, path,
                                  "An unknown error occurred.");
                return false;
            }
        }

        static void finalizeSavedDocument(cupuacu::State *state,
                                         const std::filesystem::path &path,
                                         const file::AudioExportSettings &settings,
                                         const bool updateCurrentFile)
        {
            if (!state)
            {
                return;
            }

            auto &session = state->getActiveDocumentSession();
            if (updateCurrentFile)
            {
                session.setCurrentFile(path.string(), settings);
            }
            else
            {
                session.currentFileExportSettings = settings;
                session.setPreservationReference(path.string(), settings);
            }

            file::OverwritePreservation::refreshActiveSession(state);
            if (session.document.getSampleFormat() ==
                    cupuacu::SampleFormat::PCM_S8 ||
                session.document.getSampleFormat() ==
                    cupuacu::SampleFormat::PCM_S16 ||
                session.document.getSampleFormat() ==
                    cupuacu::SampleFormat::FLOAT32)
            {
                session.document.markCurrentStateAsSavedSource();
            }
        }
    } // namespace detail

    static bool saveAs(cupuacu::State *state, const std::string &absoluteFilePath,
                       const file::AudioExportSettings &settings);
    static bool saveAsPreserving(cupuacu::State *state,
                                 const std::string &absoluteFilePath,
                                 const file::AudioExportSettings &settings);

    static bool overwrite(cupuacu::State *state)
    {
        if (!state)
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.currentFile.empty())
        {
            return false;
        }

        auto settings = session.currentFileExportSettings;
        if (!settings.has_value())
        {
            settings = file::defaultExportSettingsForPath(
                session.currentFile, session.document.getSampleFormat());
        }
        if (!settings.has_value())
        {
            return false;
        }

        const bool ok = detail::runSaveOperation(
            state, "Save", session.currentFile,
            [&]
            {
                file::AudioFileWriter::writeFile(state, session.currentFile,
                                                 *settings);
            });
        if (!ok)
        {
            return false;
        }

        detail::finalizeSavedDocument(
            state, std::filesystem::path(session.currentFile), *settings, false);
        return true;
    }

    static bool overwritePreserving(cupuacu::State *state)
    {
        if (!state)
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.currentFile.empty())
        {
            return false;
        }

        auto settings = session.currentFileExportSettings;
        if (!settings.has_value())
        {
            settings = file::defaultExportSettingsForPath(
                session.currentFile, session.document.getSampleFormat());
        }
        if (!settings.has_value())
        {
            return false;
        }

        const auto plan =
            file::SaveWritePlanner::planPreservingOverwrite(state, *settings);
        if (plan.mode != file::SaveWriteMode::OverwritePreservingRewrite)
        {
            detail::reportSaveFailure(
                state, "Preserving overwrite", session.currentFile,
                plan.preservationUnavailableReason.value_or(
                    "Preserving overwrite is unavailable"));
            return false;
        }

        const bool ok = detail::runSaveOperation(
            state, "Preserving overwrite", session.currentFile,
            [&]
            {
                file::overwritePreservingCurrentFile(state, *settings);
            });
        if (!ok)
        {
            return false;
        }

        detail::finalizeSavedDocument(
            state, std::filesystem::path(session.currentFile), *settings, false);
        return true;
    }

    static bool saveAs(cupuacu::State *state, const std::string &absoluteFilePath)
    {
        if (!state || absoluteFilePath.empty())
        {
            return false;
        }

        const auto settings = file::defaultExportSettingsForPath(
            absoluteFilePath, state->getActiveDocumentSession().document.getSampleFormat());
        if (!settings.has_value())
        {
            return false;
        }

        return saveAs(state, absoluteFilePath, *settings);
    }

    static bool saveAs(cupuacu::State *state, const std::string &absoluteFilePath,
                       const file::AudioExportSettings &settings)
    {
        if (!state || absoluteFilePath.empty() || !settings.isValid())
        {
            return false;
        }

        const auto normalizedPath =
            file::normalizeExportPath(absoluteFilePath, settings);
        const bool ok = detail::runSaveOperation(
            state, "Save", normalizedPath.string(),
            [&]
            {
                file::AudioFileWriter::writeFile(state, normalizedPath, settings);
            });
        if (!ok)
        {
            return false;
        }

        detail::finalizeSavedDocument(state, normalizedPath, settings, true);
        rememberRecentFile(state, normalizedPath.string());
        setMainWindowTitle(state, normalizedPath.string());
        return true;
    }

    static bool saveAsPreserving(cupuacu::State *state,
                                 const std::string &absoluteFilePath,
                                 const file::AudioExportSettings &settings)
    {
        if (!state || absoluteFilePath.empty() || !settings.isValid())
        {
            return false;
        }

        const auto normalizedPath =
            file::normalizeExportPath(absoluteFilePath, settings);
        const auto plan =
            file::SaveWritePlanner::planPreservingSaveAs(state, settings);
        if (plan.mode != file::SaveWriteMode::OverwritePreservingRewrite)
        {
            detail::reportSaveFailure(
                state, "Preserving save as", normalizedPath.string(),
                plan.preservationUnavailableReason.value_or(
                    "Preserving save as is unavailable"));
            return false;
        }

        const bool ok = detail::runSaveOperation(
            state, "Preserving save as", normalizedPath.string(),
            [&]
            {
                const auto &session = state->getActiveDocumentSession();
                const auto referencePath =
                    !session.preservationReferenceFile.empty()
                        ? std::filesystem::path(session.preservationReferenceFile)
                        : std::filesystem::path(session.currentFile);
                file::writePreservingFile(state, referencePath, normalizedPath,
                                          settings);
            });
        if (!ok)
        {
            return false;
        }

        detail::finalizeSavedDocument(state, normalizedPath, settings, true);
        rememberRecentFile(state, normalizedPath.string());
        setMainWindowTitle(state, normalizedPath.string());
        return true;
    }
} // namespace cupuacu::actions
