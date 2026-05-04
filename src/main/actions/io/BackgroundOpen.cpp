#include "BackgroundOpen.hpp"

#include "../../LongTask.hpp"
#include "../../file/OverwritePreservation.hpp"
#include "../../gui/Window.hpp"
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
                new BackgroundOpenJob(id, std::move(request)));
            const auto detail = state->backgroundOpenJob->getPath();
            cupuacu::setLongTask(state, "Opening file", detail, 0.0,
                                 false);
            state->backgroundOpenJob->start();
        }

        void renderMainWindowNow(cupuacu::State *state)
        {
            if (!state || !state->mainWindowInitialFrameRendered ||
                !state->mainDocumentSessionWindow)
            {
                return;
            }

            auto *window = state->mainDocumentSessionWindow->getWindow();
            if (!window || !window->isOpen())
            {
                return;
            }

            window->renderFrame();
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

        void commitCompletedBackgroundOpen(cupuacu::State *state,
                                           BackgroundOpenJob &job)
        {
            const auto snapshot = job.snapshot();

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

            if (!prepareTabForOpenedDocument(state))
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
            auto &session = state->getActiveDocumentSession();
            session.setCurrentFile(snapshot.path);
            cupuacu::file::commitLoadedAudioFile(session, snapshot.path,
                                                 std::move(*loaded));
            cupuacu::file::OverwritePreservation::refreshActiveSession(state);
            refreshDocumentUi(state);
            if (isStartupRestore &&
                snapshot.request.persistedDocumentState.has_value())
            {
                applyPersistedOpenDocumentState(
                    state, *snapshot.request.persistedDocumentState);
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
            setMainWindowTitle(state, snapshot.path);
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
                };
                cupuacu::updateLongTask(state, "Building waveform cache",
                                        progress, false);
                renderMainWindowNow(state);
                return;
            }

            cupuacu::clearLongTask(state, false);
            if (isStartupRestore)
            {
                renderMainWindowNow(state);
            }
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
                setMainWindowTitle(
                    state,
                    state->getActiveDocumentSession().currentFile.empty()
                        ? kUntitledDocumentTitle
                        : state->getActiveDocumentSession().currentFile);
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
            detail::reportStartupRestoreFailures(state, failures);
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
                                         PendingOpenRequest requestToOpen)
        : id(idToUse),
          request(std::move(requestToOpen)),
          detail(request.path)
    {
    }

    BackgroundOpenJob::~BackgroundOpenJob()
    {
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
            auto loaded = std::make_unique<file::LoadedAudioFile>(
                file::loadAudioFile(request.path,
                                    [this](const std::string &detailToUse,
                                           std::optional<double> progressToUse)
                                    {
                                        publishProgress(detailToUse,
                                                        progressToUse);
                                    }));
            std::lock_guard lock(mutex);
            loadedFile = std::move(loaded);
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

    void processPendingOpenWork(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        if (state->backgroundOpenJob)
        {
            const auto snapshot = state->backgroundOpenJob->snapshot();
            if (snapshot.completed)
            {
                auto job = std::move(state->backgroundOpenJob);
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
                const bool cacheStateChanged = session.pumpWaveformCacheWork();
                if (const auto progress =
                        normalizedWaveformCacheBuildProgress(session);
                    progress.has_value())
                {
                    cupuacu::updateLongTask(state, "Building waveform cache",
                                            progress, false);
                    if (cacheStateChanged && tabIndex == state->activeTabIndex)
                    {
                        renderMainWindowNow(state);
                    }
                    return;
                }

                state->pendingOpenWaveformBuild = {};
                cupuacu::clearLongTask(state, false);
                if (cacheStateChanged && tabIndex == state->activeTabIndex)
                {
                    renderMainWindowNow(state);
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
