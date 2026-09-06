#include "../../file/DecodedImportCache.hpp"
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

            const bool interactive = request.kind == PendingOpenKind::UserOpen;
            if (interactive)
            {
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
            }
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
            if (interactive)
            {
                state->getActiveTab()->operation =
                    DocumentOperation{.kind = DocumentOperation::Kind::Import,
                                      .id = id,
                                      .title = "Opening file",
                                      .detail = detail,
                                      .progress = 0.0};
            }
            else
            {
                setLongTask(state, "Opening file", detail, 0.0, false, true);
            }
            state->backgroundOpenJob->start(state->taskScheduler);
        }

        std::optional<double> normalizedWaveformCacheBuildProgress(
            const cupuacu::DocumentSession &session)
        {
            const auto progress = session.getWaveformCacheBuildProgress();
            if (!progress.has_value())
            {
                return std::nullopt;
            }
            if (progress->totalBlocks <= 0)
            {
                return 1.0;
            }
            return std::clamp(static_cast<double>(progress->completedBlocks) /
                                  static_cast<double>(progress->totalBlocks),
                              0.0, 1.0);
        }

        void cancelPendingOpenWaveformBuild(cupuacu::State *state)
        {
            if (!state || !state->pendingOpenWaveformBuild.active)
            {
                return;
            }

            auto pending = std::move(state->pendingOpenWaveformBuild);
            state->pendingOpenWaveformBuild = {};
            if (pending.tabIndex >= 0 &&
                pending.tabIndex < static_cast<int>(state->tabs.size()))
            {
                state->tabs[static_cast<std::size_t>(pending.tabIndex)]
                    .session.stopWaveformCacheBuild();
            }

            if (pending.revertOnCancel)
            {
                state->tabs = std::move(pending.previousTabs);
                state->recentFiles = std::move(pending.previousRecentFiles);
                if (state->tabs.empty())
                {
                    state->tabs.emplace_back();
                }
                state->activeTabIndex =
                    std::clamp(pending.previousActiveTabIndex, 0,
                               static_cast<int>(state->tabs.size()) - 1);
                bindMainWindowToActiveDocument(state);
                refreshBoundDocumentUi(state);
                setMainWindowTitleToActiveDocument(state);
            }

            cupuacu::clearLongTask(state, false);
        }

        void commitCompletedBackgroundOpen(cupuacu::State *state,
                                           BackgroundOpenJob &job)
        {
            const auto snapshot = job.snapshot();
            const bool hasPreview =
                state->pendingOpenWaveformBuild.active &&
                state->getActiveDocumentSession().openingPreview;
            const auto previousTabs =
                hasPreview ? state->pendingOpenWaveformBuild.previousTabs
                           : state->tabs;
            const auto previousRecentFiles =
                hasPreview ? state->pendingOpenWaveformBuild.previousRecentFiles
                           : state->recentFiles;
            const int previousActiveTabIndex =
                hasPreview
                    ? state->pendingOpenWaveformBuild.previousActiveTabIndex
                    : state->activeTabIndex;

            const auto recordStartupFailure = [&]()
            {
                state->startupRestore.failures.push_back(
                    {.path = snapshot.path,
                     .reason = detail::condensedRestoreReason(snapshot.path,
                                                              snapshot.error)});
                state->recentFiles.erase(std::remove(state->recentFiles.begin(),
                                                     state->recentFiles.end(),
                                                     snapshot.path),
                                         state->recentFiles.end());
                --state->startupRestore.remaining;
            };

            const auto recordStartupCommitFailure =
                [&](const std::string &reason)
            {
                state->startupRestore.failures.push_back(
                    {.path = snapshot.path, .reason = reason});
                --state->startupRestore.remaining;
            };

            const bool isStartupRestore =
                snapshot.request.kind == PendingOpenKind::StartupRestore;

            if (!snapshot.success)
            {
                if (hasPreview)
                {
                    cancelPendingOpenWaveformBuild(state);
                }
                cupuacu::clearLongTask(state, false);
                const bool showUi = !isStartupRestore;
                detail::reportDocumentIoFailure(state, "Open", snapshot.path,
                                                snapshot.error, showUi);
                if (isStartupRestore)
                {
                    recordStartupFailure();
                }
                return;
            }

            auto loaded = job.takeLoadedFile();
            if (!loaded)
            {
                cupuacu::clearLongTask(state, false);
                detail::reportDocumentIoFailure(
                    state, "Open", snapshot.path,
                    "The background open job did not produce a document.",
                    !isStartupRestore);
                if (isStartupRestore)
                {
                    recordStartupCommitFailure(
                        "The background open job did not produce a document.");
                }
                return;
            }

            if (!hasPreview && !prepareTabForOpenedDocument(state))
            {
                cupuacu::clearLongTask(state, false);
                detail::reportDocumentIoFailure(
                    state, "Open", snapshot.path,
                    "Could not prepare a tab for the opened document.",
                    !isStartupRestore);
                if (isStartupRestore)
                {
                    recordStartupCommitFailure(
                        "Could not prepare a tab for the opened document.");
                }
                return;
            }

            prepareForDocumentTransition(state);
            state->pendingOpenWaveformBuild = {};
            auto &session = state->getActiveDocumentSession();
            session.setCurrentFile(snapshot.path);
            cupuacu::file::commitLoadedAudioFile(
                session, snapshot.path, std::move(*loaded), state->paths.get());
            cupuacu::file::OverwritePreservation::refreshActiveSession(state);
            refreshDocumentUi(state);
            if (isStartupRestore &&
                snapshot.request.persistedDocumentState.has_value())
            {
                applyPersistedOpenDocumentState(
                    state, *snapshot.request.persistedDocumentState);
                if (!snapshot.request.persistedDocumentState->undoStorePath
                         .empty())
                {
                    if (!cupuacu::undo::restoreUndoManifest(
                            state, static_cast<int>(state->tabs.size()) - 1,
                            snapshot.request.persistedDocumentState
                                ->undoStorePath))
                    {
                        state->startupRestore.historyRestoreFailed = true;
                    }
                }
                if (snapshot.request.targetTabIndex ==
                    state->startupRestore.activeOpenFileIndex)
                {
                    state->startupRestore.restoredActiveTabIndex =
                        static_cast<int>(state->tabs.size()) - 1;
                }
                --state->startupRestore.remaining;
            }
            else if (isStartupRestore)
            {
                --state->startupRestore.remaining;
            }
            setMainWindowTitleToActiveDocument(state);
            if (snapshot.request.updateRecentFiles)
            {
                rememberRecentFile(state, snapshot.path);
            }
            session.updateWaveformCache();
            if (const auto progress =
                    normalizedWaveformCacheBuildProgress(session);
                progress.has_value())
            {
                state->pendingOpenWaveformBuild = {
                    .active = true,
                    .request = snapshot.request,
                    .path = snapshot.path,
                    .tabIndex = state->activeTabIndex,
                    .revertOnCancel =
                        snapshot.request.kind == PendingOpenKind::UserOpen,
                    .previousTabs = previousTabs,
                    .previousRecentFiles = previousRecentFiles,
                    .previousActiveTabIndex = previousActiveTabIndex,
                };
                cupuacu::updateLongTask(state, "Building waveform cache",
                                        progress, false);
                return;
            }

            cupuacu::clearLongTask(state, false);
        }

        void applyOpeningPreview(State *state, BackgroundOpenJob &job,
                                 waveform::DecodedWaveformChunk chunk)
        {
            if (!state->pendingOpenWaveformBuild.active)
            {
                auto previousTabs = state->tabs;
                const auto previousIndex = state->activeTabIndex;
                if (!prepareTabForOpenedDocument(state))
                {
                    job.cancel();
                    return;
                }
                state->pendingOpenWaveformBuild = {
                    .active = true,
                    .request = job.getRequest(),
                    .path = job.getPath(),
                    .tabIndex = state->activeTabIndex,
                    .revertOnCancel = true,
                    .previousTabs = std::move(previousTabs),
                    .previousRecentFiles = state->recentFiles,
                    .previousActiveTabIndex = previousIndex};
                auto &session = state->getActiveDocumentSession();
                session.openingPreview = true;
                session.document.setExternalAudioShape(
                    chunk.format, chunk.sampleRate, chunk.channels.size(),
                    chunk.frameCount);
                session.waveformCaches.resetToChannelCount(
                    chunk.channels.size());
                session.setCurrentFile(job.getPath());
                session.syncSelectionAndCursorToDocumentLength();
                refreshDocumentUi(state);
                setMainWindowTitleToActiveDocument(state);
            }
            auto &session = state->getActiveDocumentSession();
            if (chunk.audio)
            {
                session.openingAudio =
                    concurrency::releaseOnWorker(chunk.audio);
            }
            if (session.openingPeaks.get() != chunk.progressivePeaks.get() ||
                session.openingCachedPeaks.get() != chunk.sourcePeaks.get())
            {
                session.openingPeaks =
                    concurrency::releaseOnWorker(chunk.progressivePeaks);
                session.openingCachedPeaks =
                    concurrency::releaseOnWorker(chunk.sourcePeaks);
                session.invalidateViewportSource();
            }
            if (chunk.cached)
            {
                session.waveformCaches = std::move(*chunk.cached);
            }
            else if (chunk.toBlock >= chunk.fromBlock)
            {
                for (std::size_t c = 0; c < chunk.channels.size(); ++c)
                {
                    session.getWaveformCache(c).applyLevelSpanUpdates(
                        chunk.frameCount, chunk.fromBlock, chunk.toBlock,
                        chunk.channels[c]);
                }
            }
            gui::Waveform::applyAllPendingCacheUpdates(state);
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
                        chunk->format, chunk->sampleRate,
                        chunk->channels.size(), chunk->frameCount);
                    session.waveformCaches.resetToChannelCount(
                        chunk->channels.size());
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
                if (chunk->cached)
                {
                    session.waveformCaches = std::move(*chunk->cached);
                }
                else if (chunk->toBlock >= chunk->fromBlock)
                {
                    for (std::size_t c = 0; c < chunk->channels.size(); ++c)
                    {
                        session.getWaveformCache(c).applyLevelSpanUpdates(
                            chunk->frameCount, chunk->fromBlock, chunk->toBlock,
                            chunk->channels[c]);
                    }
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
            auto *tab = findOperationTab(state, request.targetTabId);
            if (!tab || !tab->operation ||
                tab->operation->id != retired->getId() ||
                tab->operation->kind != DocumentOperation::Kind::Import)
            {
                return;
            }
            if (!snapshot.success)
            {
                removeImportTab(state, request, retired->getId());
                if (!snapshot.canceled)
                {
                    detail::reportDocumentIoFailure(
                        state, "Open", snapshot.path, snapshot.error, true);
                }
                return;
            }
            auto loaded = retired->takeLoadedFile();
            if (!loaded)
            {
                removeImportTab(state, request, retired->getId());
                return;
            }
            const bool hadPreview = tab->session.document.getChannelCount() > 0;
            const auto selection = tab->session.selection;
            const auto cursor = tab->session.cursor;
            file::commitLoadedAudioFile(tab->session, snapshot.path,
                                        std::move(*loaded), state->paths.get());
            if (hadPreview)
            {
                tab->session.selection = selection;
                tab->session.cursor = cursor;
            }
            finishOperation(state, request.targetTabId,
                            DocumentOperation::Kind::Import, retired->getId());
            const auto index = int(tab - state->tabs.data());
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
                state->backgroundOpenJob || !state->pendingOpenFiles.empty())
            {
                return;
            }

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
            if (state->getActiveDocumentSession().currentFile.empty())
            {
                state->getActiveDocumentSession().clearCurrentFile();
                if (state->startupRestore.shouldPersistState)
                {
                    persistSessionState(state);
                }
            }
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

    std::optional<waveform::DecodedWaveformChunk>
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

    void BackgroundOpenJob::publishPreview(waveform::DecodedWaveformChunk chunk)
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
            std::unique_ptr<file::LoadedAudioFile> loaded;
            if (request.persistedDocumentState &&
                !request.persistedDocumentState->undoStorePath.empty())
            {
                loaded =
                    std::make_unique<file::LoadedAudioFile>(file::loadAudioFile(
                        request.path,
                        [this](const auto &text, auto value)
                        {
                            publishProgress(text, value);
                        },
                        [this]
                        {
                            return cancelRequested.load();
                        }));
            }
            else
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

        for (auto &tab : state->tabs)
        {
            tab.session.retryImportedPeakPersistence();
        }

        if (state->quitRequestedAfterLongTaskCancel)
        {
            state->pendingOpenFiles.clear();
            if (state->pendingOpenWaveformBuild.active)
            {
                cancelPendingOpenWaveformBuild(state);
            }
        }

        if (state->backgroundOpenJob)
        {
            if (state->backgroundOpenJob->getRequest().kind ==
                PendingOpenKind::UserOpen)
            {
                processInteractiveOpen(state);
                return;
            }

            if (cupuacu::isLongTaskCancelRequested(state))
            {
                state->backgroundOpenJob->cancel();
            }
            // Bound UI work even when decoding outruns rendering. The producer
            // waits on its eight-entry queue and cancellation wakes that wait.
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
            for (int count = 0; count < 8 && !isLongTaskCancelRequested(state);
                 ++count)
            {
                auto preview = state->backgroundOpenJob->takePreview();
                if (!preview)
                {
                    break;
                }
                applyOpeningPreview(state, *state->backgroundOpenJob,
                                    std::move(*preview));
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    break;
                }
            }
            const auto snapshot = state->backgroundOpenJob->snapshot();
            if (snapshot.completed)
            {
                auto job = concurrency::releaseOnWorker(
                    std::shared_ptr<BackgroundOpenJob>(
                        state->backgroundOpenJob.release()));
                if (snapshot.canceled)
                {
                    cancelPendingOpenWaveformBuild(state);
                    cupuacu::clearLongTask(state, false);
                    if (snapshot.request.kind ==
                            PendingOpenKind::StartupRestore ||
                        state->quitRequestedAfterLongTaskCancel)
                    {
                        if (state->quitRequestedAfterLongTaskCancel)
                        {
                            state->preserveStartupSessionStateOnShutdown = true;
                        }
                        state->startupRestore = {};
                        state->pendingOpenFiles.clear();
                    }
                    return;
                }
                commitCompletedBackgroundOpen(state, *job);
                finalizeStartupRestoreIfComplete(state);
            }
            else
            {
                cupuacu::updateLongTask(state, snapshot.detail,
                                        snapshot.progress, false);
            }
            return;
        }

        if (state->pendingOpenWaveformBuild.active)
        {
            if (cupuacu::isLongTaskCancelRequested(state))
            {
                cancelPendingOpenWaveformBuild(state);
                if (state->quitRequestedAfterLongTaskCancel)
                {
                    state->preserveStartupSessionStateOnShutdown = true;
                    state->startupRestore = {};
                    state->pendingOpenFiles.clear();
                }
                return;
            }
            const int tabIndex = state->pendingOpenWaveformBuild.tabIndex;
            if (tabIndex < 0 ||
                tabIndex >= static_cast<int>(state->tabs.size()))
            {
                state->pendingOpenWaveformBuild = {};
                cupuacu::clearLongTask(state, false);
            }
            else
            {
                auto &session =
                    state->tabs[static_cast<std::size_t>(tabIndex)].session;
                const bool cacheStateChanged =
                    session.pumpWaveformCacheWork(state->paths.get());
                if (const auto progress =
                        normalizedWaveformCacheBuildProgress(session);
                    progress.has_value())
                {
                    if (cacheStateChanged && tabIndex == state->activeTabIndex)
                    {
                        cupuacu::gui::Waveform::applyAllPendingCacheUpdates(
                            state);
                    }
                    cupuacu::updateLongTask(state, "Building waveform cache",
                                            progress, false);
                    return;
                }

                state->pendingOpenWaveformBuild = {};
                cupuacu::clearLongTask(state, false);
                if (cacheStateChanged && tabIndex == state->activeTabIndex)
                {
                    cupuacu::gui::Waveform::applyAllPendingCacheUpdates(state);
                }
            }
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
