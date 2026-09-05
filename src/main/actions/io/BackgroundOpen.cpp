#include "BackgroundOpen.hpp"

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

            const auto id = nextBackgroundOpenJobId();
            state->backgroundOpenJob.reset(
                new BackgroundOpenJob(
                    id, std::move(request),
                    state->paths ? state->paths->waveformCachePath()
                                 : std::filesystem::path{}));
            const auto detail = state->backgroundOpenJob->getPath();
            cupuacu::setLongTask(state, "Opening file", detail, 0.0,
                                 false, true);
            state->backgroundOpenJob->start();
        }

        std::optional<double>
        normalizedWaveformCacheBuildProgress(
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
            return std::clamp(
                static_cast<double>(progress->completedBlocks) /
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
                state->activeTabIndex = std::clamp(
                    pending.previousActiveTabIndex, 0,
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
                     .reason = detail::condensedRestoreReason(
                         snapshot.path, snapshot.error)});
                state->recentFiles.erase(
                    std::remove(state->recentFiles.begin(),
                                state->recentFiles.end(), snapshot.path),
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
            cupuacu::file::commitLoadedAudioFile(session, snapshot.path,
                                                 std::move(*loaded),
                                                 state->paths.get());
            cupuacu::file::OverwritePreservation::refreshActiveSession(state);
            refreshDocumentUi(state);
            if (isStartupRestore &&
                snapshot.request.persistedDocumentState.has_value())
            {
                applyPersistedOpenDocumentState(
                    state, *snapshot.request.persistedDocumentState);
                if (!snapshot.request.persistedDocumentState->undoStorePath.empty())
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
                session.document.initialize(chunk.format, chunk.sampleRate,
                                            chunk.channels.size(),
                                            chunk.frameCount);
                session.waveformCaches.resetToChannelCount(
                    chunk.channels.size());
                session.setCurrentFile(job.getPath());
                session.syncSelectionAndCursorToDocumentLength();
                refreshDocumentUi(state);
                setMainWindowTitleToActiveDocument(state);
            }
            auto &session = state->getActiveDocumentSession();
            if (chunk.cached)
            {
                session.waveformCaches = std::move(*chunk.cached);
            }
            else
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

        void finalizeStartupRestoreIfComplete(cupuacu::State *state)
        {
            if (!state || !state->startupRestore.active ||
                state->startupRestore.remaining > 0 || state->backgroundOpenJob ||
                !state->pendingOpenFiles.empty())
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

    BackgroundOpenJob::BackgroundOpenJob(std::uint64_t idToUse,
                                         PendingOpenRequest requestToOpen,
                                         std::filesystem::path
                                             waveformCacheRootToUse)
        : id(idToUse),
          request(std::move(requestToOpen)),
          waveformCacheRoot(std::move(waveformCacheRootToUse)),
          detail(request.path)
    {
    }

    BackgroundOpenJob::~BackgroundOpenJob()
    {
        cancel();
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void BackgroundOpenJob::start()
    {
        worker = std::thread([this]
                             { run(); });
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

    void BackgroundOpenJob::publishProgress(
        const std::string &detailToUse, std::optional<double> progressToUse)
    {
        std::lock_guard lock(mutex);
        detail = detailToUse;
        progress = progressToUse;
    }

    void BackgroundOpenJob::run()
    {
        try
        {
            waveform::DecodedWaveformBuilder builder;
            waveform::DocumentWaveformCaches cached;
            bool cacheChecked = false;
            bool cacheLoaded = false;
            auto loaded =
                std::make_unique<file::LoadedAudioFile>(file::loadAudioFile(
                    request.path,
                    [this](const std::string &detailToUse,
                           std::optional<double> progressToUse)
                    {
                        publishProgress(detailToUse, progressToUse);
                    },
                    [this]()
                    {
                        return cancelRequested.load();
                    },
                    [&](const Document &document, int64_t frames)
                    {
                        if (cancelRequested.load())
                        {
                            throw LongTaskCanceledError{};
                        }
                        if (!cacheChecked)
                        {
                            cacheChecked = true;
                            if (!waveformCacheRoot.empty())
                            {
                                DocumentSession cacheSession;
                                cacheSession.currentFile = request.path;
                                cacheSession.document = document;
                                cacheLoaded =
                                    waveform::loadPersistentWaveformCache(
                                        cacheSession, waveformCacheRoot);
                                if (cacheLoaded)
                                {
                                    cached =
                                        std::move(cacheSession.waveformCaches);
                                }
                            }
                            if (cacheLoaded)
                            {
                                waveform::DecodedWaveformChunk preview;
                                preview.format = document.getSampleFormat();
                                preview.sampleRate = document.getSampleRate();
                                preview.frameCount = document.getFrameCount();
                                preview.channels.resize(
                                    document.getChannelCount());
                                preview.cached = cached;
                                publishPreview(std::move(preview));
                            }
                        }
                        if (!cacheLoaded)
                        {
                            if (auto chunk = builder.append(document, frames))
                            {
                                publishPreview(std::move(*chunk));
                            }
                        }
                    }));
            if (cancelRequested.load())
            {
                throw LongTaskCanceledError{};
            }
            loaded->persistentWaveformCacheChecked = cacheChecked;
            loaded->persistentWaveformCacheLoaded = cacheLoaded;
            loaded->waveformCachesReady = loaded->document.getFrameCount() > 0;
            loaded->waveformCaches =
                cacheLoaded ? std::move(cached) : builder.takeCaches();
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
                auto job = std::move(state->backgroundOpenJob);
                if (snapshot.canceled)
                {
                    cancelPendingOpenWaveformBuild(state);
                    cupuacu::clearLongTask(state, false);
                    if (snapshot.request.kind == PendingOpenKind::StartupRestore ||
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
            if (tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()))
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
                        cupuacu::gui::Waveform::applyAllPendingCacheUpdates(state);
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

        if (!state->pendingOpenFiles.empty())
        {
            auto request = std::move(state->pendingOpenFiles.front());
            state->pendingOpenFiles.pop_front();
            startBackgroundOpen(state, std::move(request));
        }
        finalizeStartupRestoreIfComplete(state);
    }
} // namespace cupuacu::actions::io
