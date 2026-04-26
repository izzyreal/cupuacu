#include "BackgroundSave.hpp"

#include "../../LongTask.hpp"
#include "../../file/AudioFileWriter.hpp"
#include "../../file/OverwritePreservation.hpp"
#include "../../file/PreservationBackend.hpp"
#include "../../file/SaveWritePlan.hpp"
#include "../DocumentLifecycle.hpp"
#include "../Save.hpp"

#include <exception>
#include <utility>

namespace cupuacu::actions::io
{
    namespace
    {
        std::uint64_t nextBackgroundSaveJobId()
        {
            static std::uint64_t nextId = 1;
            return nextId++;
        }

        const char *operationForKind(const BackgroundSaveKind kind)
        {
            switch (kind)
            {
                case BackgroundSaveKind::OverwritePreserving:
                    return "Preserving overwrite";
                case BackgroundSaveKind::SaveAsPreserving:
                    return "Preserving save as";
                case BackgroundSaveKind::Overwrite:
                case BackgroundSaveKind::SaveAs:
                default:
                    return "Save";
            }
        }

        bool updatesCurrentFile(const BackgroundSaveKind kind)
        {
            return kind == BackgroundSaveKind::SaveAs ||
                   kind == BackgroundSaveKind::SaveAsPreserving;
        }

        void startBackgroundSave(cupuacu::State *state,
                                 BackgroundSaveRequest request,
                                 const cupuacu::Document *document = nullptr)
        {
            if (!state || request.path.empty() || state->backgroundSaveJob)
            {
                return;
            }

            const auto id = nextBackgroundSaveJobId();
            const auto detail = request.path.string();
            state->backgroundSaveJob.reset(
                new BackgroundSaveJob(id, std::move(request), state, document));
            cupuacu::setLongTask(state, "Saving file", detail, 0.0, false);
            state->backgroundSaveJob->start();
        }

        bool canStartSave(cupuacu::State *state)
        {
            return state != nullptr && !state->backgroundSaveJob &&
                   !state->backgroundOpenJob && !state->longTask.active;
        }

        void commitCompletedBackgroundSave(cupuacu::State *state,
                                           const BackgroundSaveJob::Snapshot &snapshot)
        {
            cupuacu::clearLongTask(state, false);
            if (!snapshot.success)
            {
                detail::reportSaveFailure(
                    state, operationForKind(snapshot.request.kind),
                    snapshot.request.path.string(), snapshot.error);
                return;
            }

            detail::finalizeSavedDocument(
                state, snapshot.request.path, snapshot.request.settings,
                updatesCurrentFile(snapshot.request.kind));
            if (updatesCurrentFile(snapshot.request.kind))
            {
                rememberRecentFile(state, snapshot.request.path.string());
                setMainWindowTitle(state, snapshot.request.path.string());
            }
            else
            {
                persistSessionState(state);
            }
        }
    } // namespace

    BackgroundSaveJob::BackgroundSaveJob(std::uint64_t idToUse,
                                         BackgroundSaveRequest requestToSave,
                                         cupuacu::State *stateToUse,
                                         const cupuacu::Document *documentToWrite)
        : id(idToUse),
          request(std::move(requestToSave)),
          state(stateToUse),
          document(documentToWrite),
          detail(request.path.string())
    {
    }

    BackgroundSaveJob::~BackgroundSaveJob()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void BackgroundSaveJob::start()
    {
        worker = std::thread([this]
                             { run(); });
    }

    BackgroundSaveJob::Snapshot BackgroundSaveJob::snapshot() const
    {
        std::lock_guard lock(mutex);
        return {
            .completed = completed,
            .success = success,
            .request = request,
            .detail = detail,
            .progress = progress,
            .error = error,
        };
    }

    std::uint64_t BackgroundSaveJob::getId() const
    {
        return id;
    }

    void BackgroundSaveJob::publishProgress(
        const std::string &detailToUse, std::optional<double> progressToUse)
    {
        std::lock_guard lock(mutex);
        detail = detailToUse;
        progress = progressToUse;
    }

    void BackgroundSaveJob::run()
    {
        try
        {
            const auto progressCallback =
                [this](const std::string &detailToUse,
                       std::optional<double> progressToUse)
            {
                publishProgress(detailToUse, progressToUse);
            };

            switch (request.kind)
            {
                case BackgroundSaveKind::Overwrite:
                case BackgroundSaveKind::SaveAs:
                {
                    if (document == nullptr)
                    {
                        throw std::runtime_error(
                            "Background save job has no document to write");
                    }
                    const auto lease = document->acquireReadLease();
                    file::AudioFileWriter::writeFile(
                        lease, request.path, request.settings, progressCallback);
                    break;
                }
                case BackgroundSaveKind::OverwritePreserving:
                case BackgroundSaveKind::SaveAsPreserving:
                {
                    if (document == nullptr)
                    {
                        throw std::runtime_error(
                            "Background preserving save job has no document to write");
                    }
                    if (request.referencePath.empty())
                    {
                        throw std::runtime_error(
                            "Background preserving save job has no reference file");
                    }
                    const auto lease = document->acquireReadLease();
                    file::writePreservingFile(file::PreservationWriteInput{
                        .document = lease,
                        .referencePath = request.referencePath,
                        .outputPath = request.path,
                        .settings = request.settings,
                        .progress = progressCallback,
                    });
                    break;
                }
            }

            std::lock_guard lock(mutex);
            success = true;
            completed = true;
        }
        catch (const std::exception &e)
        {
            std::lock_guard lock(mutex);
            error = e.what();
            success = false;
            completed = true;
        }
        catch (...)
        {
            std::lock_guard lock(mutex);
            error = "An unknown error occurred.";
            success = false;
            completed = true;
        }
    }

    bool queueOverwrite(cupuacu::State *state)
    {
        if (!canStartSave(state))
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
        if (!settings.has_value() ||
            !detail::confirmMarkerPersistenceIfNeeded(state, *settings))
        {
            return false;
        }

        startBackgroundSave(state,
                            BackgroundSaveRequest{
                                .kind = BackgroundSaveKind::Overwrite,
                                .path = session.currentFile,
                                .settings = *settings,
                            },
                            &session.document);
        return true;
    }

    bool queueOverwritePreserving(cupuacu::State *state)
    {
        if (!canStartSave(state))
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
        if (!detail::confirmMarkerPersistenceIfNeeded(state, *settings))
        {
            return false;
        }

        const auto referencePath =
            !session.preservationReferenceFile.empty()
                ? std::filesystem::path(session.preservationReferenceFile)
                : std::filesystem::path(session.currentFile);
        startBackgroundSave(
            state,
            BackgroundSaveRequest{
                .kind = BackgroundSaveKind::OverwritePreserving,
                .path = session.currentFile,
                .referencePath = referencePath,
                .settings = *settings,
            },
            &session.document);
        return true;
    }

    bool queueSaveAs(cupuacu::State *state,
                     const std::string &absoluteFilePath,
                     const file::AudioExportSettings &settings)
    {
        if (!canStartSave(state) || absoluteFilePath.empty() ||
            !settings.isValid())
        {
            return false;
        }

        const auto normalizedPath =
            file::normalizeExportPath(absoluteFilePath, settings);
        if (!state->pendingSaveAsMarkerWarningConfirmed &&
            !detail::confirmMarkerPersistenceIfNeeded(state, settings))
        {
            return false;
        }

        startBackgroundSave(state,
                            BackgroundSaveRequest{
                                .kind = BackgroundSaveKind::SaveAs,
                                .path = normalizedPath,
                                .settings = settings,
                            },
                            &state->getActiveDocumentSession().document);
        return true;
    }

    bool queueSaveAsPreserving(cupuacu::State *state,
                               const std::string &absoluteFilePath,
                               const file::AudioExportSettings &settings)
    {
        if (!canStartSave(state) || absoluteFilePath.empty() ||
            !settings.isValid())
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
        if (!state->pendingSaveAsMarkerWarningConfirmed &&
            !detail::confirmMarkerPersistenceIfNeeded(state, settings))
        {
            return false;
        }

        const auto &session = state->getActiveDocumentSession();
        const auto referencePath =
            !session.preservationReferenceFile.empty()
                ? std::filesystem::path(session.preservationReferenceFile)
                : std::filesystem::path(session.currentFile);
        startBackgroundSave(
            state, BackgroundSaveRequest{
                       .kind = BackgroundSaveKind::SaveAsPreserving,
                       .path = normalizedPath,
                       .referencePath = referencePath,
                       .settings = settings,
                   },
            &session.document);
        return true;
    }

    void processPendingSaveWork(cupuacu::State *state)
    {
        if (!state || !state->backgroundSaveJob)
        {
            return;
        }

        const auto snapshot = state->backgroundSaveJob->snapshot();
        if (snapshot.completed)
        {
            auto job = std::move(state->backgroundSaveJob);
            commitCompletedBackgroundSave(state, snapshot);
            return;
        }

        cupuacu::updateLongTask(state, snapshot.detail, snapshot.progress,
                                false);
    }
} // namespace cupuacu::actions::io
