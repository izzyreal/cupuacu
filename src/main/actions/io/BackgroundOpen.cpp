#include "../../file/DecodedImportCache.hpp"
#include "../../persistence/LegacyRecovery.hpp"
#include "BackgroundOpen.hpp"
#include "../DocumentOperationAccess.hpp"
#include "../DocumentTabs.hpp"
#include "../../file/OwnedAudioImport.hpp"
#include "../../concurrency/DeferredRelease.hpp"

#include "../../LongTask.hpp"
#include "../../file/OverwritePreservation.hpp"
#include "../../gui/Waveform.hpp"
#include "../../undo/UndoManifestPersistence.hpp"
#include "../../gui/Window.hpp"
#include "../../waveform/WaveformCachePersistence.hpp"
#include "../DocumentLifecycle.hpp"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace cupuacu::actions::io
{
    namespace
    {
        std::uint64_t nextBackgroundOpenJobId()
        {
            static std::uint64_t nextId = 1;
            return nextId++;
        }

        void startBackgroundOpen(cupuacu::State *state,
                                 PendingOpenRequest request)
        {
            if (!state || request.path.empty() || state->backgroundOpenJob)
            {
                return;
            }

            request.previousActiveTabId = state->getActiveTab()->id;
            if (!prepareTabForOpenedDocument(state))
            {
                state->pendingOpenFiles.push_front(std::move(request));
                return;
            }
            auto &tab = *state->getActiveTab();
            request.targetTabId = tab.id;
            tab.session.setCurrentFile(request.path);
            tab.session.openingPreview = true;
            if (!state->decodedImportCache && state->paths)
            {
                state->decodedImportCache =
                    std::make_shared<file::DecodedImportCache>(
                        state->paths->statePath() / "decoded-cache",
                        state->decodedImportCacheByteBudget);
            }
            const auto id = nextBackgroundOpenJobId();
            state->backgroundOpenJob.reset(new BackgroundOpenJob(
                id, std::move(request),
                state->paths ? state->paths->waveformCachePath()
                             : std::filesystem::path{},
                state->paths ? state->paths->statePath()
                             : std::filesystem::temp_directory_path(),
                state->importSampleCache, state->decodedImportCache));
            const auto detail = state->backgroundOpenJob->getPath();
            state->getActiveTab()->operation =
                DocumentOperation{.kind = DocumentOperation::Kind::Import,
                                  .id = id,
                                  .title = "Opening file",
                                  .detail = detail,
                                  .progress = 0.0};
            state->backgroundOpenJob->start(state->taskScheduler);
        }

        void removeImportTab(State *state, const PendingOpenRequest &request,
                             uint64_t jobId)
        {
            auto *target = findOperationTab(state, request.targetTabId);
            if (!target || !target->operation ||
                target->operation->id != jobId ||
                target->operation->kind != DocumentOperation::Kind::Import)
            {
                return;
            }
            const auto activeId = state->getActiveTab()->id;
            if (activeId == request.targetTabId && state->audioDevices &&
                state->audioDevices->isPlaying())
            {
                requestStop(state);
            }
            const auto desired = activeId == request.targetTabId
                                     ? request.previousActiveTabId
                                     : activeId;
            std::erase_if(state->tabs,
                          [&](const auto &tab)
                          {
                              return tab.id == request.targetTabId;
                          });
            if (state->tabs.empty())
            {
                state->tabs.emplace_back();
            }
            state->activeTabIndex = std::clamp(state->activeTabIndex, 0,
                                               int(state->tabs.size()) - 1);
            for (std::size_t i = 0; i < state->tabs.size(); ++i)
            {
                if (state->tabs[i].id == desired)
                {
                    state->activeTabIndex = int(i);
                }
            }
            refreshActiveTabUi(state);
        }
        void processInteractiveOpen(State *state)
        {
            auto &job = *state->backgroundOpenJob;
            const auto request = job.getRequest();
            if (operationCanceled(state, request.targetTabId,
                                  DocumentOperation::Kind::Import,
                                  job.getId()) ||
                state->quitRequestedAfterLongTaskCancel)
            {
                job.cancel();
            }
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
            for (int n = 0; n < 8; ++n)
            {
                auto chunk = job.takePreview();
                if (!chunk)
                {
                    break;
                }
                auto *tab = findOperationTab(state, request.targetTabId);
                if (!tab || !tab->operation ||
                    tab->operation->kind != DocumentOperation::Kind::Import ||
                    tab->operation->id != job.getId())
                {
                    break;
                }
                auto &session = tab->session;
                const bool initial = session.document.getChannelCount() == 0;
                if (initial)
                {
                    session.document.setExternalAudioShape(
                        chunk->shape.format, chunk->shape.sampleRate,
                        chunk->shape.channels, chunk->shape.frames);
                    session.waveformCaches.resetToChannelCount(
                        chunk->shape.channels);
                    session.syncSelectionAndCursorToDocumentLength();
                }
                if (chunk->audio &&
                    session.openingAudio.get() != chunk->audio.get())
                {
                    session.openingAudio =
                        concurrency::releaseOnWorker(std::move(chunk->audio));
                }
                if (session.openingPeaks.get() !=
                        chunk->progressivePeaks.get() ||
                    session.openingCachedPeaks.get() !=
                        chunk->sourcePeaks.get())
                {
                    session.openingPeaks =
                        concurrency::releaseOnWorker(chunk->progressivePeaks);
                    session.openingCachedPeaks =
                        concurrency::releaseOnWorker(chunk->sourcePeaks);
                    session.invalidateViewportSource();
                }
                if (state->getActiveTab()->id == tab->id)
                {
                    if (initial)
                    {
                        refreshDocumentUi(state);
                    }
                    gui::Waveform::applyAllPendingCacheUpdates(state);
                }
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    break;
                }
            }
            auto snapshot = job.snapshot();
            updateOperation(state, request.targetTabId,
                            DocumentOperation::Kind::Import, job.getId(),
                            snapshot.detail, snapshot.progress);
            if (!snapshot.completed)
            {
                return;
            }
            auto retired =
                concurrency::releaseOnWorker(std::shared_ptr<BackgroundOpenJob>(
                    state->backgroundOpenJob.release()));
            if (request.kind == PendingOpenKind::StartupRestore)
            {
                --state->startupRestore.remaining;
                if (!snapshot.success && !snapshot.canceled)
                {
                    state->startupRestore.failures.push_back(
                        {snapshot.path, snapshot.error});
                }
            }
            auto *tab = findOperationTab(state, request.targetTabId);
            if (!tab || !tab->operation ||
                tab->operation->id != retired->getId() ||
                tab->operation->kind != DocumentOperation::Kind::Import)
            {
                return;
            }
            if (!snapshot.success || tab->operation->cancelRequested)
            {
                removeImportTab(state, request, retired->getId());
                if (!snapshot.canceled)
                {
                    detail::reportDocumentIoFailure(
                        state, "Open", snapshot.path, snapshot.error,
                        request.kind != PendingOpenKind::StartupRestore);
                }
                return;
            }
            auto loaded = retired->takeLoadedFile();
            auto restored = retired->takeRestoredSession();
            const bool fromSnapshot = bool(restored);
            if (!loaded && !restored)
            {
                removeImportTab(state, request, retired->getId());
                return;
            }
            const bool hadPreview = tab->session.document.getChannelCount() > 0;
            const auto selection = tab->session.selection;
            const auto cursor = tab->session.cursor;
            if (restored)
            {
                tab->session = std::move(*restored);
            }
            else
            {
                file::commitLoadedAudioFile(tab->session, snapshot.path,
                                            std::move(*loaded),
                                            state->paths.get());
            }
            if (hadPreview && !fromSnapshot)
            {
                tab->session.selection = selection;
                tab->session.cursor = cursor;
            }
            finishOperation(state, request.targetTabId,
                            DocumentOperation::Kind::Import, retired->getId());
            const auto index = int(tab - state->tabs.data());
            if (request.kind == PendingOpenKind::StartupRestore)
            {
                if (request.targetTabIndex ==
                    state->startupRestore.activeOpenFileIndex)
                {
                    state->startupRestore.restoredActiveTabIndex = index;
                }
                if (fromSnapshot && tab->session.hasReadRevision())
                {
                    if (!persistence::RevisionPersistence::installHistory(
                            state, index))
                    {
                        state->startupRestore.historyRestoreFailed = true;
                    }
                }
                else if (request.persistedDocumentState)
                {
                    // Metadata application targets this tab even if another
                    // tab became active while I/O ran.
                    applyPersistedOpenDocumentState(
                        state, *request.persistedDocumentState, index);
                    if (!request.persistedDocumentState->undoStorePath
                             .empty() &&
                        !undo::restoreUndoManifest(
                            state, index,
                            request.persistedDocumentState->undoStorePath))
                    {
                        state->startupRestore.historyRestoreFailed = true;
                    }
                }
            }
            file::OverwritePreservation::refreshSession(state, index);
            if (index == state->activeTabIndex)
            {
                if (hadPreview)
                {
                    refreshBoundDocumentUi(state);
                }
                else
                {
                    refreshDocumentUi(state);
                }
                setMainWindowTitleToActiveDocument(state);
            }
            if (request.updateRecentFiles)
            {
                rememberRecentFile(state, snapshot.path);
            }
        }

        void finalizeStartupRestoreIfComplete(cupuacu::State *state)
        {
            if (!state || !state->startupRestore.active ||
                state->startupRestore.remaining > 0 ||
                state->startupClipboardRestore || state->backgroundOpenJob ||
                !state->pendingOpenFiles.empty())
            {
                return;
            }

            state->startupRestore.active = false;
            if (state->startupRestore.restoredActiveTabIndex >= 0 &&
                state->startupRestore.restoredActiveTabIndex <
                    static_cast<int>(state->tabs.size()))
            {
                state->activeTabIndex =
                    state->startupRestore.restoredActiveTabIndex;
                bindMainWindowToActiveDocument(state);
                refreshBoundDocumentUi(state);
                setMainWindowTitleToActiveDocument(state);
            }

            std::vector<detail::DocumentRestoreFailure> failures;
            failures.reserve(state->startupRestore.failures.size());
            for (const auto &failure : state->startupRestore.failures)
            {
                failures.push_back({failure.path, failure.reason});
            }

            if (state->startupRestore.shouldPersistState)
            {
                persistSessionState(state);
            }
            detail::reportStartupRestoreOutcome(
                state, failures, state->startupRestore.clipboardRestoreFailed,
                state->startupRestore.historyRestoreFailed);
            state->startupRestore = {};
        }
    } // namespace

    void queueOpenFile(cupuacu::State *state, std::string path)
    {
        queueOpenRequest(state, PendingOpenRequest{
                                    .kind = PendingOpenKind::UserOpen,
                                    .path = std::move(path),
                                    .targetTabIndex = -1,
                                    .updateRecentFiles = true,
                                });
    }

    void queueOpenRequest(cupuacu::State *state, PendingOpenRequest request)
    {
        if (!state)
        {
            return;
        }
        if (request.path.empty())
        {
            return;
        }
        state->pendingOpenFiles.push_back(std::move(request));
    }

    BackgroundOpenJob::BackgroundOpenJob(
        std::uint64_t idToUse, PendingOpenRequest requestToOpen,
        std::filesystem::path waveformCacheRootToUse,
        std::filesystem::path workingRootToUse,
        std::shared_ptr<storage::DecodedBlockCache> cacheToUse,
        std::shared_ptr<file::DecodedImportCache> decodedCacheToUse)
        : id(idToUse), request(std::move(requestToOpen)),
          waveformCacheRoot(std::move(waveformCacheRootToUse)),
          workingRoot(workingRootToUse.empty()
                          ? std::filesystem::temp_directory_path()
                          : std::move(workingRootToUse)),
          sampleCache(std::move(cacheToUse)),
          decodedCache(std::move(decodedCacheToUse)), detail(request.path)
    {
    }

    BackgroundOpenJob::~BackgroundOpenJob()
    {
        cancel();
        if (completion.valid())
        {
            completion.wait();
        }
    }

    void BackgroundOpenJob::start(
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

    BackgroundOpenJob::Snapshot BackgroundOpenJob::snapshot() const
    {
        std::lock_guard lock(mutex);
        return {
            .completed = completed,
            .success = success,
            .canceled = cancelRequested.load() && completed && !success,
            .request = request,
            .path = request.path,
            .detail = detail,
            .progress = progress,
            .error = error,
        };
    }

    std::unique_ptr<file::LoadedAudioFile> BackgroundOpenJob::takeLoadedFile()
    {
        std::lock_guard lock(mutex);
        return std::move(loadedFile);
    }

    std::optional<waveform::ImportPreview>
    BackgroundOpenJob::takePreview()
    {
        std::lock_guard lock(mutex);
        if (previews.empty())
        {
            return std::nullopt;
        }
        auto result = std::move(previews.front());
        previews.pop_front();
        previewCv.notify_one();
        return result;
    }

    std::unique_ptr<DocumentSession> BackgroundOpenJob::takeRestoredSession()
    {
        std::lock_guard lock(mutex);
        return std::move(restoredSession);
    }

    void BackgroundOpenJob::publishPreview(waveform::ImportPreview chunk)
    {
        std::unique_lock lock(mutex);
        // These notifications carry shared readers, not deltas. Retaining
        // the latest availability is sufficient even when the UI is busy.
        if (chunk.progressivePeaks && !previews.empty() &&
            previews.back().progressivePeaks == chunk.progressivePeaks)
        {
            previews.back() = std::move(chunk);
            return;
        }
        previewCv.wait(lock,
                       [this]
                       {
                           return previews.size() < 8 || cancelRequested.load();
                       });
        if (cancelRequested.load())
        {
            throw LongTaskCanceledError{};
        }
        previews.push_back(std::move(chunk));
    }

    std::uint64_t BackgroundOpenJob::getId() const
    {
        return id;
    }

    const std::string &BackgroundOpenJob::getPath() const
    {
        return request.path;
    }

    const PendingOpenRequest &BackgroundOpenJob::getRequest() const
    {
        return request;
    }

    void BackgroundOpenJob::cancel()
    {
        cancelRequested.store(true);
        previewCv.notify_all();
    }

    void BackgroundOpenJob::publishProgress(const std::string &detailToUse,
                                            std::optional<double> progressToUse)
    {
        std::lock_guard lock(mutex);
        detail = detailToUse;
        progress = progressToUse;
    }

    void BackgroundOpenJob::run()
    {
        try
        {
            if (cancelRequested.load())
            {
                throw LongTaskCanceledError{};
            }
            if (request.persistedDocumentState &&
                !request.persistedDocumentState->autosaveSnapshotPath.empty())
            {
                auto restored = std::make_unique<DocumentSession>();
                if (!persistence::loadDocumentAutosaveSnapshot(
                        request.persistedDocumentState->autosaveSnapshotPath,
                        *restored,
                        [this](auto value)
                        {
                            publishProgress("Restoring document", value);
                        },
                        [this]
                        {
                            return cancelRequested.load();
                        },
                        &*request.persistedDocumentState))
                {
                    throw std::runtime_error(
                        "Autosave snapshot could not be read");
                }
                if (cancelRequested.load())
                {
                    throw LongTaskCanceledError{};
                }
                std::lock_guard lock(mutex);
                restoredSession = std::move(restored);
                success = completed = true;
                return;
            }
            std::unique_ptr<file::LoadedAudioFile> loaded;
            {
                if (!sampleCache)
                {
                    sampleCache = storage::defaultDecodedBlockCache();
                }
                const auto identity =
                    file::DecodedImportCache::sourceIdentity(request.path);
                if (decodedCache)
                {
                    publishProgress("Checking decoded audio: " +
                                        std::filesystem::path(request.path)
                                            .filename()
                                            .string(),
                                    {});
                    loaded =
                        decodedCache->load(identity, sampleCache,
                                           [this]
                                           {
                                               return cancelRequested.load();
                                           });
                    if (loaded && file::DecodedImportCache::sourceIdentity(
                                      request.path) != identity)
                    {
                        throw std::runtime_error(
                            "Source changed during import");
                    }
                    if (loaded)
                    {
                        publishProgress("Audio ready: " +
                                            std::filesystem::path(request.path)
                                                .filename()
                                                .string(),
                                        1.0);
                    }
                }
                if (!loaded)
                {
                    const auto directory =
                        workingRoot /
                        ("import-" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()) +
                         "-" + std::to_string(id));
                    auto imported = file::importOwnedAudio(
                        request.path, directory, sampleCache,
                        [this](const auto &text, auto value)
                        {
                            publishProgress(text, value);
                        },
                        [this]
                        {
                            return cancelRequested.load();
                        },
                        [this](auto chunk)
                        {
                            publishPreview(std::move(chunk));
                        },
                        {.preferFilesystemClone = true,
                         .waveformCacheRoot = waveformCacheRoot,
                         .publishMetadata = true,
                         .publishAudio =
                             request.kind == PendingOpenKind::UserOpen});
                    loaded = std::make_unique<file::LoadedAudioFile>(
                        std::move(imported.metadata));
                    loaded->audioRevision =
                        storage::AudioEditRevision::from(imported.audio);
                    loaded->ownedSource = std::move(imported.audio);
                    loaded->waveformCaches = {};
                    if (file::DecodedImportCache::sourceIdentity(
                            request.path) != identity)
                    {
                        throw std::runtime_error(
                            "Source changed during import");
                    }
                    if (decodedCache)
                    {
                        // Optional cache persistence must never fail a valid
                        // import.
                        try
                        {
                            decodedCache->retain(identity, *loaded, scheduler);
                        }
                        catch (const std::exception &)
                        {
                        }
                    }
                }
            }
            if (request.persistedDocumentState &&
                !request.persistedDocumentState->undoStorePath.empty())
            {
                auto restored = std::make_unique<DocumentSession>();
                file::commitLoadedAudioFile(*restored, request.path,
                                            std::move(*loaded));
                if (!persistence::migrateLegacyHistory(
                        *restored, &*request.persistedDocumentState,
                        workingRoot,
                        [this]
                        {
                            return cancelRequested.load();
                        }))
                {
                    throw std::runtime_error("Legacy history migration failed");
                }
                restored->undoStore.attach(
                    request.persistedDocumentState->undoStorePath);
                restoredSession = std::move(restored);
                loaded.reset();
            }
            if (cancelRequested.load())
            {
                throw LongTaskCanceledError{};
            }
            std::lock_guard lock(mutex);
            loadedFile = std::move(loaded);
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

    void processPendingOpenWork(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        processStartupClipboardRestore(state);
        for (auto &tab : state->tabs)
        {
            tab.session.retryImportedPeakPersistence(state->taskScheduler);
        }

        if (state->quitRequestedAfterLongTaskCancel)
        {
            state->pendingOpenFiles.clear();
        }

        if (state->backgroundOpenJob)
        {
            processInteractiveOpen(state);
            finalizeStartupRestoreIfComplete(state);
            return;
        }

        if (!state->pendingOpenFiles.empty() && !state->longTask.active &&
            !state->revisionRecording)
        {
            auto request = std::move(state->pendingOpenFiles.front());
            state->pendingOpenFiles.pop_front();
            startBackgroundOpen(state, std::move(request));
        }
        finalizeStartupRestoreIfComplete(state);
    }
} // namespace cupuacu::actions::io
