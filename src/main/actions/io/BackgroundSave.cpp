#include "BackgroundSave.hpp"
#include "../DocumentOperationAccess.hpp"
#include "../../file/OwnedSourceFile.hpp"
#include "../../persistence/RevisionPersistence.hpp"
#include "../../concurrency/DeferredRelease.hpp"

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
            if (!state || !document || request.path.empty())
            {
                return;
            }

            const auto id = nextBackgroundSaveJobId();
            const auto detail = request.path.string();
            decltype(state->backgroundSaveJob) job{
                new BackgroundSaveJob(
                    id, std::move(request), state, *document,
                    state->paths ? state->paths->waveformCachePath()
                                 : std::filesystem::path{},
                    state->getActiveDocumentSession().getEditRevision()),
                destroyBackgroundSaveJob};
            if (state->getActiveDocumentSession().hasReadRevision())
            {
                state->getActiveTab()->operation =
                    DocumentOperation{.kind = DocumentOperation::Kind::Save,
                                      .id = id,
                                      .title = "Saving file",
                                      .detail = detail,
                                      .progress = 0.0};
            }
            else
            {
                setLongTask(state, "Saving file", detail, 0.0, false, true);
            }
            job->start(state->taskScheduler);
            if (state->backgroundSaveJob)
            {
                state->additionalSaveJobs.push_back(std::move(job));
            }
            else
            {
                state->backgroundSaveJob = std::move(job);
            }
        }

        bool canStartSave(cupuacu::State *state)
        {
            return state && !state->revisionRecording &&
                   !state->longTask.active && state->getActiveTab() &&
                   !state->getActiveTab()->operation;
        }

        bool canRunAutosavePump(const cupuacu::State *state)
        {
            return state && !state->revisionRecording &&
                   !state->quitRequestedAfterLongTaskCancel &&
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
            return !session.openingPreview && document.getChannelCount() > 0 &&
                   std::chrono::steady_clock::now() >=
                       session.autosaveRetryAfter &&
                   !session.autosaveSnapshotPath.empty() &&
                   (session.autosavedWaveformDataVersion !=
                        document.getWaveformDataVersion() ||
                    session.autosavedMarkerDataVersion !=
                        document.getMarkerDataVersion() ||
                    (session.hasReadRevision() &&
                     session.autosavedHistoryVersion != tab.historyVersion));
        }

        void commitCompletedBackgroundSave(cupuacu::State *state,
                                           const BackgroundSaveJob::Snapshot &snapshot)
        {
            if (!snapshot.identity || !snapshot.identity->revision)
            {
                cupuacu::clearLongTask(state, false);
            }
            if (snapshot.canceled)
            {
                if (state)
                {
                    if (snapshot.identity &&
                        state->pendingCloseTabAfterSaveId ==
                            snapshot.identity->tabId)
                    {
                        state->pendingCloseTabAfterSaveId.reset();
                    }
                }
                return;
            }
            if (!snapshot.success)
            {
                if (state)
                {
                    if (snapshot.identity &&
                        state->pendingCloseTabAfterSaveId ==
                            snapshot.identity->tabId)
                    {
                        state->pendingCloseTabAfterSaveId.reset();
                    }
                }
                detail::reportSaveFailure(
                    state, operationForKind(snapshot.request.kind),
                    snapshot.request.path.string(), snapshot.error);
                return;
            }

            const int target =
                snapshot.identity
                    ? findTabIndexById(state, snapshot.identity->tabId)
                    : -1;
            if (target < 0)
            {
                if (snapshot.identity && state->pendingCloseTabAfterSaveId ==
                                             snapshot.identity->tabId)
                {
                    state->pendingCloseTabAfterSaveId.reset();
                }
                return; // The pinned revision was saved; its tab no longer
                        // exists.
            }
            auto &session = state->tabs[target].session;
            const auto &saved = *snapshot.identity;
            if (session.document.getPreservationSourceId() != saved.sourceId ||
                session.hasReadRevision() != bool(saved.revision))
            {
                if (snapshot.identity && state->pendingCloseTabAfterSaveId ==
                                             snapshot.identity->tabId)
                {
                    state->pendingCloseTabAfterSaveId.reset();
                }
                return; // The tab has been reused for another document.
            }
            const bool matches =
                saved.revision
                    ? session.getEditRevision() == saved.revision &&
                          session.document.getMarkers() == saved.markers
                    : session.document.getWaveformDataVersion() ==
                              saved.audioVersion &&
                          session.document.getMarkerDataVersion() ==
                              saved.markerVersion;
            if (!saved.revision && !matches)
            {
                // Resident mutations retain their current filename/autosave.
                // Never clear newer work when an older snapshot completes.
                if (snapshot.identity && state->pendingCloseTabAfterSaveId ==
                                             snapshot.identity->tabId)
                {
                    state->pendingCloseTabAfterSaveId.reset();
                }
                return;
            }
            if (saved.revision && session.hasReadRevision())
            {
                session.markRevisionSaved(saved.revision, saved.markers);
                session.preservationSource =
                    concurrency::releaseOnWorker(snapshot.savedContainer);
            }
            detail::finalizeSavedDocument(
                state, snapshot.request.path, snapshot.request.settings,
                updatesCurrentFile(snapshot.request.kind),
                snapshot.persistentWaveformCacheSaved, target, matches);
            if (updatesCurrentFile(snapshot.request.kind))
                rememberRecentFile(state, snapshot.request.path.string());
            else
                persistSessionState(state);
            if (state->activeTabIndex == target)
            {
                setMainWindowTitle(state, session.currentFile);
            }
            if (!matches)
            {
                if (snapshot.identity && state->pendingCloseTabAfterSaveId ==
                                             snapshot.identity->tabId)
                {
                    state->pendingCloseTabAfterSaveId.reset();
                }
                return;
            }

            if (!state || !state->pendingCloseTabAfterSaveId.has_value() ||
                state->pendingCloseTabAfterSaveId != saved.tabId)
            {
                return;
            }

            const auto tabIndex =
                findTabIndexById(state, *state->pendingCloseTabAfterSaveId);
            if (snapshot.identity &&
                state->pendingCloseTabAfterSaveId == snapshot.identity->tabId)
            {
                state->pendingCloseTabAfterSaveId.reset();
            }
            if (tabIndex == target)
            {
                (void)closeTab(state, tabIndex);
            }
        }
    } // namespace

    BackgroundSaveJob::BackgroundSaveJob(
        std::uint64_t idToUse, BackgroundSaveRequest requestToSave,
        cupuacu::State *stateToUse, const cupuacu::Document &documentToWrite,
        std::filesystem::path waveformCacheRootToUse,
        std::shared_ptr<const storage::AudioEditRevision> revision)
        : id(idToUse), request(std::move(requestToSave)),
          document(documentToWrite),
          waveformCacheRoot(std::move(waveformCacheRootToUse)),
          workingRoot(stateToUse && stateToUse->paths
                          ? stateToUse->paths->statePath()
                          : std::filesystem::temp_directory_path()),
          detail(request.path.string())
    {
        identity = std::make_shared<Identity>(Identity{
            stateToUse && stateToUse->getActiveTab()
                ? stateToUse->getActiveTab()->id
                : 0,
            document.getWaveformDataVersion(), document.getMarkerDataVersion(),
            document.getPreservationSourceId(), std::move(revision),
            document.getMarkers(),
            stateToUse ? stateToUse->getActiveDocumentSession().preservationSource : nullptr});
    }

    BackgroundSaveJob::~BackgroundSaveJob()
    {
        cancel();
        if (completion.valid())
        {
            completion.wait();
        }
    }

    BackgroundAutosaveJob::BackgroundAutosaveJob(
        const uint64_t tabIdToUse, std::filesystem::path pathToUse,
        const uint64_t waveformDataVersionToUse,
        const uint64_t markerDataVersionToUse, std::string currentFileToUse,
        const cupuacu::Document &documentToSave,
        const waveform::DocumentWaveformCaches &cachesToSave,
        std::shared_ptr<const persistence::RevisionCheckpoint> revisionToSave)
        : tabId(tabIdToUse), path(std::move(pathToUse)),
          waveformDataVersion(waveformDataVersionToUse),
          markerDataVersion(markerDataVersionToUse),
          currentFile(std::move(currentFileToUse)), document(documentToSave),
          waveformCaches(revisionToSave ? waveform::DocumentWaveformCaches{}
                                        : cachesToSave.snapshotForDocument(
                                              documentToSave)),
          revision(std::move(revisionToSave))
    {
    }

    BackgroundAutosaveJob::~BackgroundAutosaveJob()
    {
        cancel();
        if (completion.valid())
        {
            completion.wait();
        }
    }

    void BackgroundAutosaveJob::start(
        std::shared_ptr<concurrency::TaskScheduler> executor)
    {
        scheduler = executor ? std::move(executor)
                             : concurrency::defaultTaskScheduler();
        try
        {
            completion = scheduler->submit(
                [this]
                {
                    run();
                },
                concurrency::TaskScheduler::Options{
                    .priority = concurrency::TaskScheduler::Priority::Autosave,
                    .documentId = tabId,
                    .deadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2)});
        }
        catch (const std::exception &failure)
        {
            std::lock_guard lock(mutex);
            error = failure.what();
            completed = true;
        }
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
            .historyVersion = revision ? revision->metadata.value(
                                             "historyVersion", uint64_t{0})
                                       : 0,
            .currentFile = currentFile,
            .progress = completed ? std::optional<double>(1.0) : std::nullopt,
            .error = error,
        };
    }

    void BackgroundAutosaveJob::run()
    {
        try
        {
            if (cancelRequested.load())
            {
                throw LongTaskCanceledError{};
            }
            if (revision)
            {
                persistence::RevisionPersistence::save(path, *revision);
                std::lock_guard lock(mutex);
                success = true;
                completed = true;
                return;
            }
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

    void BackgroundSaveJob::start(
        std::shared_ptr<concurrency::TaskScheduler> executor)
    {
        scheduler = executor ? std::move(executor)
                             : concurrency::defaultTaskScheduler();
        try
        {
            completion = scheduler->submit(
                [this]
                {
                    run();
                },
                concurrency::TaskScheduler::Options{});
        }
        catch (const std::exception &failure)
        {
            std::lock_guard lock(mutex);
            error = failure.what();
            completed = true;
        }
    }

    BackgroundSaveJob::Snapshot BackgroundSaveJob::snapshot() const
    {
        std::lock_guard lock(mutex);
        return {
            .identity = identity,
            .savedContainer = savedContainer,
            .completed = completed,
            .success = success,
            .canceled = cancelRequested.load() && completed && !success,
            .persistentWaveformCacheSaved = persistentWaveformCacheSaved,
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
            if (cancelRequested.load())
            {
                throw LongTaskCanceledError{};
            }
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

            auto write = [&](const std::filesystem::path &output)
            {
                switch (request.kind)
                {
                    case BackgroundSaveKind::Overwrite:
                    case BackgroundSaveKind::SaveAs:
                    {
                        if (identity->revision)
                        {
                            file::AudioFileWriter::writeFile(
                                *identity->revision, identity->markers, output,
                                request.settings, progressCallback);
                        }
                        else
                        {
                            const auto lease = document.acquireReadLease();
                            file::AudioFileWriter::writeFile(lease, output,
                                                             request.settings,
                                                             progressCallback);
                        }
                        break;
                    }
                    case BackgroundSaveKind::OverwritePreserving:
                    case BackgroundSaveKind::SaveAsPreserving:
                    {
                        if (request.referencePath.empty())
                        {
                            throw std::runtime_error(
                                "Background preserving save job has no "
                                "reference file");
                        }
                        if (identity->revision)
                        {
                            file::writePreservingRevision(
                                *identity->revision, identity->markers,
                                request.referencePath, output, request.settings,
                                progressCallback);
                        }
                        else
                        {
                            const auto lease = document.acquireReadLease();
                            file::writePreservingFile(
                                file::PreservationWriteInput{
                                    .document = lease,
                                    .referencePath = request.referencePath,
                                    .outputPath = output,
                                    .settings = request.settings,
                                    .progress = progressCallback,
                                });
                        }
                        break;
                    }
                }
            };
            if (identity->revision)
            {
                auto container = file::writeOwnedRevisionContainer(
                    request.path, workingRoot, identity->revision->shape(),
                    write,
                    [&](double progress)
                    {
                        progressCallback("Retaining saved source", progress);
                    });
                std::lock_guard lock(mutex);
                savedContainer = std::move(container);
            }
            else
            {
                write(request.path);
            }

            if (!identity->revision && !waveformCacheRoot.empty() &&
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
            session.hasReadRevision()
                ? file::revisionPreservationReference(session)
            : !session.preservationReferenceFile.empty()
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
            session.hasReadRevision()
                ? file::revisionPreservationReference(session)
            : !session.preservationReferenceFile.empty()
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
        if (!state)
        {
            return;
        }
        auto pump = [&](auto &owned)
        {
            if (!owned)
            {
                return;
            }
            auto snapshot = owned->snapshot();
            const auto tabId = snapshot.identity->tabId;
            const bool revision = bool(snapshot.identity->revision);
            const auto *tab = findOperationTab(state, tabId);
            const bool targeted =
                !revision ||
                (tab && tab->operation &&
                 tab->operation->kind == DocumentOperation::Kind::Save &&
                 tab->operation->id == owned->getId());
            if (state->quitRequestedAfterLongTaskCancel ||
                (revision && targeted && tab->operation->cancelRequested) ||
                (!revision && isLongTaskCancelRequested(state)))
            {
                owned->cancel();
            }
            if (snapshot.completed)
            {
                finishOperation(state, tabId, DocumentOperation::Kind::Save,
                                owned->getId());
                auto retired = concurrency::releaseOnWorker(
                    std::shared_ptr<BackgroundSaveJob>(owned.release()));
                if (targeted)
                {
                    commitCompletedBackgroundSave(state, snapshot);
                }
            }
            else if (revision)
            {
                updateOperation(state, tabId, DocumentOperation::Kind::Save,
                                owned->getId(), snapshot.detail,
                                snapshot.progress);
            }
            else
            {
                updateLongTask(state, snapshot.detail, snapshot.progress,
                               false);
            }
        };
        pump(state->backgroundSaveJob);
        for (auto &job : state->additionalSaveJobs)
        {
            pump(job);
        }
        std::erase_if(state->additionalSaveJobs,
                      [](const auto &job)
                      {
                          return !job;
                      });
        if (!state->backgroundSaveJob && !state->additionalSaveJobs.empty())
        {
            state->backgroundSaveJob =
                std::move(state->additionalSaveJobs.back());
            state->additionalSaveJobs.pop_back();
        }
    }

    void queueAutosaveForTab(cupuacu::State *state, const int tabIndex)
    {
        if (!state || tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()))
        {
            return;
        }

        auto &tab = state->tabs[static_cast<std::size_t>(tabIndex)];
        auto &session = tab.session;
        if (session.openingPreview || session.document.getChannelCount() <= 0 ||
            std::chrono::steady_clock::now() < session.autosaveRetryAfter)
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
                    session.waveformCaches,
                    session.hasReadRevision()
                        ? persistence::RevisionPersistence::capture(session,
                                                                    &tab)
                        : nullptr),
                cupuacu::destroyBackgroundAutosaveJob};
            state->backgroundAutosaveJob->start(state->taskScheduler);
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
                // Closing/replacing a document can discard its snapshot while
                // this worker is still writing it. Remove late output only
                // when no live session owns the path; an older revision of a
                // still-open document must remain available until its retry.
                const bool snapshotStillOwned = std::any_of(
                    state->tabs.begin(), state->tabs.end(),
                    [&](const auto &tab)
                    {
                        return tab.session.autosaveSnapshotPath == snapshot.path;
                    });
                if (!snapshotStillOwned)
                {
                    persistence::removeDocumentAutosaveSnapshot(snapshot.path);
                }
                if (snapshot.success)
                {
                    const int tabIndex = findTabIndexById(state, snapshot.tabId);
                    if (tabIndex >= 0)
                    {
                        auto &session =
                            state->tabs[static_cast<std::size_t>(tabIndex)].session;
                        session.lastAutosaveError.clear();
                        session.autosaveRetryAfter = {};
                        if (session.hasReadRevision() && snapshotStillOwned)
                        {
                            // Publish the durable path even if newer edits are
                            // already waiting for another checkpoint.
                            state->pendingAutosaveSessionPersistRequestedAt =
                                std::chrono::steady_clock::now();
                        }
                        if (session.autosaveSnapshotPath == snapshot.path &&
                            session.currentFile == snapshot.currentFile &&
                            session.document.getWaveformDataVersion() ==
                                snapshot.waveformDataVersion &&
                            session.document.getMarkerDataVersion() ==
                                snapshot.markerDataVersion &&
                            (!session.hasReadRevision() ||
                             state->tabs[tabIndex].historyVersion ==
                                 snapshot.historyVersion))
                        {
                            session.autosavedWaveformDataVersion =
                                snapshot.waveformDataVersion;
                            session.autosavedMarkerDataVersion =
                                snapshot.markerDataVersion;
                            session.autosavedHistoryVersion =
                                snapshot.historyVersion;
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

                if (!snapshot.success && snapshotStillOwned)
                {
                    const int index = findTabIndexById(state, snapshot.tabId);
                    if (index >= 0)
                    {
                        auto &session = state->tabs[index].session;
                        session.autosaveRetryAfter =
                            std::chrono::steady_clock::now() +
                            std::chrono::seconds(5);
                        if (session.lastAutosaveError != snapshot.error)
                        {
                            session.lastAutosaveError = snapshot.error;
                            detail::reportSaveFailure(state, "Autosave",
                                                      snapshot.path.string(),
                                                      snapshot.error);
                        }
                    }
                }
                auto retired = concurrency::releaseOnWorker(
                    std::shared_ptr<BackgroundAutosaveJob>(
                        state->backgroundAutosaveJob.release()));
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
                        tab.session.waveformCaches,
                        tab.session.hasReadRevision()
                            ? persistence::RevisionPersistence::capture(
                                  tab.session, &tab)
                            : nullptr),
                    cupuacu::destroyBackgroundAutosaveJob};
                state->backgroundAutosaveJob->start(state->taskScheduler);
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
