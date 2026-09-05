#include "BackgroundSave.hpp"

#include "../../LongTask.hpp"
#include "../../file/AudioFileWriter.hpp"
#include "../../file/FileIo.hpp"
#include "../../file/OverwritePreservation.hpp"
#include "../../file/PreservationBackend.hpp"
#include "../../file/SaveWritePlan.hpp"
#include "../../gui/SamplePoint.hpp"
#include "../../persistence/DocumentAutosave.hpp"
#include "../../waveform/WaveformCachePersistence.hpp"
#include "../DocumentLifecycle.hpp"
#include "../DocumentTabs.hpp"
#include "../Save.hpp"

#include <algorithm>
#include <chrono>
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

        constexpr auto kAutosaveInteractionQuietPeriod =
            std::chrono::milliseconds(300);
        constexpr auto kSessionPersistQuietPeriod =
            std::chrono::milliseconds(1500);

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
                                 const cupuacu::Document *document)
        {
            if (!state || !document || request.path.empty() ||
                state->backgroundSaveJob)
            {
                return;
            }

            const auto id = nextBackgroundSaveJobId();
            const auto detail = request.path.string();
            state->backgroundSaveJob.reset(
                new BackgroundSaveJob(
                    id, std::move(request), state, *document,
                    state->paths ? state->paths->waveformCachePath()
                                 : std::filesystem::path{}));
            cupuacu::setLongTask(state, "Saving file", detail, 0.0, false,
                                 true);
            state->backgroundSaveJob->start();
        }

        bool canStartSave(cupuacu::State *state)
        {
            return state != nullptr && !state->backgroundSaveJob &&
                   !state->backgroundOpenJob && !state->longTask.active;
        }

        bool canRunAutosavePump(const cupuacu::State *state)
        {
            return state != nullptr && !state->backgroundOpenJob &&
                   !state->backgroundSaveJob && !state->backgroundEffectJob &&
                   !state->longTask.active;
        }

        bool shouldDeferAutosaveForInteraction(const cupuacu::State *state)
        {
            if (!state)
            {
                return false;
            }

            const auto now = std::chrono::steady_clock::now();
            if (state->lastRealtimeDocumentMutationAt !=
                    std::chrono::steady_clock::time_point{} &&
                now - state->lastRealtimeDocumentMutationAt <
                    kAutosaveInteractionQuietPeriod)
            {
                return true;
            }

            if (!state->mainDocumentSessionWindow)
            {
                return false;
            }

            auto *window = state->mainDocumentSessionWindow->getWindow();
            if (!window)
            {
                return false;
            }

            return dynamic_cast<cupuacu::gui::SamplePoint *>(
                       window->getCapturingComponent()) != nullptr;
        }

        bool canRunDeferredSessionPersist(const cupuacu::State *state)
        {
            return state != nullptr && !state->backgroundOpenJob &&
                   !state->backgroundSaveJob && !state->backgroundAutosaveJob &&
                   !state->backgroundEffectJob && !state->longTask.active;
        }

        bool shouldDelaySessionPersistAfterAutosave(const cupuacu::State *state)
        {
            return state != nullptr && state->mainDocumentSessionWindow != nullptr;
        }

        int findTabIndexById(const cupuacu::State *state, const uint64_t tabId)
        {
            if (!state)
            {
                return -1;
            }

            for (int index = 0; index < static_cast<int>(state->tabs.size()); ++index)
            {
                if (state->tabs[static_cast<std::size_t>(index)].id == tabId)
                {
                    return index;
                }
            }
            return -1;
        }

        bool tabNeedsAutosave(const cupuacu::DocumentTab &tab)
        {
            const auto &session = tab.session;
            const auto &document = session.document;
            return document.getChannelCount() > 0 &&
                   !session.autosaveSnapshotPath.empty() &&
                   (session.autosavedWaveformDataVersion !=
                        document.getWaveformDataVersion() ||
                    session.autosavedMarkerDataVersion !=
                        document.getMarkerDataVersion());
        }

        void commitCompletedBackgroundSave(cupuacu::State *state,
                                           const BackgroundSaveJob::Snapshot &snapshot)
        {
            cupuacu::clearLongTask(state, false);
            if (snapshot.canceled)
            {
                if (state)
                {
                    state->pendingCloseTabAfterSaveId.reset();
                }
                return;
            }
            if (!snapshot.success)
            {
                if (state)
                {
                    state->pendingCloseTabAfterSaveId.reset();
                }
                detail::reportSaveFailure(
                    state, operationForKind(snapshot.request.kind),
                    snapshot.request.path.string(), snapshot.error);
                return;
            }

            detail::finalizeSavedDocument(
                state, snapshot.request.path, snapshot.request.settings,
                updatesCurrentFile(snapshot.request.kind),
                snapshot.persistentWaveformCacheSaved);
            if (updatesCurrentFile(snapshot.request.kind))
            {
                rememberRecentFile(state, snapshot.request.path.string());
                setMainWindowTitle(state, snapshot.request.path.string());
            }
            else
            {
                persistSessionState(state);
            }

            if (!state || !state->pendingCloseTabAfterSaveId.has_value())
            {
                return;
            }

            const auto tabIndex =
                findTabIndexById(state, *state->pendingCloseTabAfterSaveId);
            state->pendingCloseTabAfterSaveId.reset();
            if (tabIndex >= 0)
            {
                (void)closeTab(state, tabIndex);
            }
        }
    } // namespace

    BackgroundSaveJob::BackgroundSaveJob(std::uint64_t idToUse,
                                         BackgroundSaveRequest requestToSave,
                                         cupuacu::State *stateToUse,
                                         const cupuacu::Document &documentToWrite,
                                         std::filesystem::path
                                             waveformCacheRootToUse)
        : id(idToUse),
          request(std::move(requestToSave)),
          state(stateToUse),
          document(documentToWrite),
          waveformCacheRoot(std::move(waveformCacheRootToUse)),
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

    BackgroundAutosaveJob::BackgroundAutosaveJob(
        const uint64_t tabIdToUse, std::filesystem::path pathToUse,
        const uint64_t waveformDataVersionToUse,
        const uint64_t markerDataVersionToUse, std::string currentFileToUse,
        const cupuacu::Document &documentToSave,
        const waveform::DocumentWaveformCaches &cachesToSave)
        : tabId(tabIdToUse), path(std::move(pathToUse)),
          waveformDataVersion(waveformDataVersionToUse),
          markerDataVersion(markerDataVersionToUse),
          currentFile(std::move(currentFileToUse)), document(documentToSave),
          waveformCaches(cachesToSave.snapshotForDocument(documentToSave))
    {
    }

    BackgroundAutosaveJob::~BackgroundAutosaveJob()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void BackgroundAutosaveJob::start()
    {
        worker = std::thread([this]
                             { run(); });
    }

    auto BackgroundAutosaveJob::snapshot() const -> Snapshot
    {
        std::lock_guard lock(mutex);
        return {
            .completed = completed,
            .success = success,
            .tabId = tabId,
            .path = path,
            .waveformDataVersion = waveformDataVersion,
            .markerDataVersion = markerDataVersion,
            .currentFile = currentFile,
            .progress = completed ? std::optional<double>(1.0) : std::nullopt,
            .error = error,
        };
    }

    void BackgroundAutosaveJob::run()
    {
        try
        {
            cupuacu::DocumentSession snapshotSession;
            snapshotSession.document = document;
            snapshotSession.currentFile = currentFile;
            snapshotSession.waveformCaches = std::move(waveformCaches);
            snapshotSession.rebuildWaveformCacheSynchronously();
            const bool saved =
                cupuacu::persistence::saveDocumentAutosaveSnapshot(
                    path, snapshotSession);

            std::lock_guard lock(mutex);
            success = saved;
            completed = true;
            if (!saved)
            {
                error = "Failed to write autosave snapshot";
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard lock(mutex);
            success = false;
            completed = true;
            error = e.what();
        }
        catch (...)
        {
            std::lock_guard lock(mutex);
            success = false;
            completed = true;
            error = "An unknown error occurred.";
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
            .canceled = cancelRequested.load() && completed && !success,
            .persistentWaveformCacheSaved =
                persistentWaveformCacheSaved,
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

    void BackgroundSaveJob::cancel()
    {
        cancelRequested.store(true);
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
                if (cancelRequested.load())
                {
                    throw cupuacu::LongTaskCanceledError{};
                }
                publishProgress(detailToUse, progressToUse);
            };

            switch (request.kind)
            {
                case BackgroundSaveKind::Overwrite:
                case BackgroundSaveKind::SaveAs:
                {
                    const auto lease = document.acquireReadLease();
                    file::AudioFileWriter::writeFile(
                        lease, request.path, request.settings, progressCallback);
                    break;
                }
                case BackgroundSaveKind::OverwritePreserving:
                case BackgroundSaveKind::SaveAsPreserving:
                {
                    if (request.referencePath.empty())
                    {
                        throw std::runtime_error(
                            "Background preserving save job has no reference file");
                    }
                    const auto lease = document.acquireReadLease();
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

            if (!waveformCacheRoot.empty() &&
                !cancelRequested.load(std::memory_order_acquire))
            {
                publishProgress("Caching waveform", std::nullopt);
                cupuacu::DocumentSession cacheSession;
                cacheSession.currentFile = request.path.string();
                cacheSession.document = document;
                cacheSession.waveformCaches.resetToChannelCount(
                    cacheSession.document.getChannelCount());
                cacheSession.rebuildWaveformCacheSynchronously();
                const bool cacheSaved =
                    cupuacu::waveform::savePersistentWaveformCache(
                        cacheSession, waveformCacheRoot);
                std::lock_guard lock(mutex);
                persistentWaveformCacheSaved = cacheSaved;
            }

            std::lock_guard lock(mutex);
            success = true;
            completed = true;
        }
        catch (const cupuacu::LongTaskCanceledError &e)
        {
            std::lock_guard lock(mutex);
            error = e.what();
            success = false;
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
        if (session.currentFileRequiresSaveAs)
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
        if (session.currentFileRequiresSaveAs)
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

        if (cupuacu::isLongTaskCancelRequested(state))
        {
            state->backgroundSaveJob->cancel();
        }

        const auto snapshot = state->backgroundSaveJob->snapshot();
        if (snapshot.completed)
        {
            auto job = std::move(state->backgroundSaveJob);
            job.reset();
            commitCompletedBackgroundSave(state, snapshot);
            return;
        }

        cupuacu::updateLongTask(state, snapshot.detail, snapshot.progress,
                                false);
    }

    void queueAutosaveForTab(cupuacu::State *state, const int tabIndex)
    {
        if (!state || tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()))
        {
            return;
        }

        auto &tab = state->tabs[static_cast<std::size_t>(tabIndex)];
        auto &session = tab.session;
        if (session.document.getChannelCount() <= 0)
        {
            return;
        }
        if (session.autosaveSnapshotPath.empty())
        {
            session.autosaveSnapshotPath =
                cupuacu::actions::detail::makeAutosaveSnapshotPath(state);
        }
        if (session.autosaveSnapshotPath.empty())
        {
            return;
        }
        if (!state->backgroundAutosaveJob && canRunAutosavePump(state))
        {
            state->backgroundAutosaveJob = {
                new BackgroundAutosaveJob(
                    tab.id, session.autosaveSnapshotPath,
                    session.document.getWaveformDataVersion(),
                    session.document.getMarkerDataVersion(),
                    session.currentFile, session.document,
                    session.waveformCaches),
                cupuacu::destroyBackgroundAutosaveJob};
            state->backgroundAutosaveJob->start();
        }
    }

    void processPendingAutosaveWork(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        const bool deferForInteraction = shouldDeferAutosaveForInteraction(state);

        if (state->backgroundAutosaveJob)
        {
            const auto snapshot = state->backgroundAutosaveJob->snapshot();
            if (snapshot.completed)
            {
                if (snapshot.success)
                {
                    const int tabIndex = findTabIndexById(state, snapshot.tabId);
                    if (tabIndex >= 0)
                    {
                        auto &session =
                            state->tabs[static_cast<std::size_t>(tabIndex)].session;
                        if (session.autosaveSnapshotPath == snapshot.path &&
                            session.currentFile == snapshot.currentFile &&
                            session.document.getWaveformDataVersion() ==
                                snapshot.waveformDataVersion &&
                            session.document.getMarkerDataVersion() ==
                                snapshot.markerDataVersion)
                        {
                            session.autosavedWaveformDataVersion =
                                snapshot.waveformDataVersion;
                            session.autosavedMarkerDataVersion =
                                snapshot.markerDataVersion;
                            if (shouldDelaySessionPersistAfterAutosave(state))
                            {
                                state->pendingAutosaveSessionPersistRequestedAt =
                                    std::chrono::steady_clock::now();
                            }
                            else
                            {
                                persistSessionState(state);
                                state->pendingAutosaveSessionPersistRequestedAt = {};
                            }
                        }
                    }
                }

                state->backgroundAutosaveJob.reset();
            }
        }

        if (!deferForInteraction && !state->backgroundAutosaveJob &&
            canRunAutosavePump(state))
        {
            for (auto &tab : state->tabs)
            {
                if (!tabNeedsAutosave(tab))
                {
                    continue;
                }

                state->backgroundAutosaveJob = {
                    new BackgroundAutosaveJob(
                        tab.id, tab.session.autosaveSnapshotPath,
                        tab.session.document.getWaveformDataVersion(),
                        tab.session.document.getMarkerDataVersion(),
                        tab.session.currentFile, tab.session.document,
                        tab.session.waveformCaches),
                    cupuacu::destroyBackgroundAutosaveJob};
                state->backgroundAutosaveJob->start();
                break;
            }
        }

        if (state->pendingAutosaveSessionPersistRequestedAt !=
                std::chrono::steady_clock::time_point{} &&
            canRunDeferredSessionPersist(state) &&
            !deferForInteraction &&
            std::chrono::steady_clock::now() -
                    state->pendingAutosaveSessionPersistRequestedAt >=
                kSessionPersistQuietPeriod)
        {
            persistSessionState(state);
            state->pendingAutosaveSessionPersistRequestedAt = {};
        }
    }
} // namespace cupuacu::actions::io
