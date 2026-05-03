#include "BackgroundSave.hpp"

#include "../../LongTask.hpp"
#include "../../file/AudioFileWriter.hpp"
#include "../../file/FileIo.hpp"
#include "../../file/OverwritePreservation.hpp"
#include "../../file/PreservationBackend.hpp"
#include "../../file/SaveWritePlan.hpp"
#include "../DocumentLifecycle.hpp"
#include "../Save.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
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

        constexpr char kAutosaveMagic[] = "CUPUACU_AUTOSAVE";
        constexpr uint32_t kAutosaveVersion = 1;
        constexpr int64_t kAutosaveFramesPerChunk = 32768;
        constexpr auto kAutosavePumpBudget = std::chrono::milliseconds(8);

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

        bool canRunAutosavePump(const cupuacu::State *state)
        {
            return state != nullptr && !state->backgroundOpenJob &&
                   !state->backgroundSaveJob && !state->backgroundEffectJob &&
                   !state->longTask.active;
        }

        void writeU32(std::ostream &output, const uint32_t value)
        {
            const char bytes[] = {
                static_cast<char>(value & 0xffu),
                static_cast<char>((value >> 8) & 0xffu),
                static_cast<char>((value >> 16) & 0xffu),
                static_cast<char>((value >> 24) & 0xffu),
            };
            output.write(bytes, sizeof(bytes));
        }

        void writeI64(std::ostream &output, const int64_t value)
        {
            const auto unsignedValue = static_cast<uint64_t>(value);
            for (int shift = 0; shift < 64; shift += 8)
            {
                output.put(static_cast<char>((unsignedValue >> shift) & 0xffu));
            }
        }

        void writeU64(std::ostream &output, const uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                output.put(static_cast<char>((value >> shift) & 0xffu));
            }
        }

        void writeString(std::ostream &output, const std::string &value)
        {
            writeU32(output, static_cast<uint32_t>(value.size()));
            output.write(value.data(), static_cast<std::streamsize>(value.size()));
        }

        void writeFloat(std::ostream &output, const float value)
        {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(output, bits);
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

    BackgroundAutosaveJob::BackgroundAutosaveJob(
        const uint64_t tabIdToUse, std::filesystem::path pathToUse,
        const uint64_t waveformDataVersionToUse,
        const uint64_t markerDataVersionToUse, std::string currentFileToUse)
        : tabId(tabIdToUse), path(std::move(pathToUse)),
          waveformDataVersion(waveformDataVersionToUse),
          markerDataVersion(markerDataVersionToUse),
          currentFile(std::move(currentFileToUse))
    {
    }

    auto BackgroundAutosaveJob::snapshot() const -> Snapshot
    {
        return {
            .completed = completed,
            .success = success,
            .tabId = tabId,
            .path = path,
            .waveformDataVersion = waveformDataVersion,
            .markerDataVersion = markerDataVersion,
            .currentFile = currentFile,
            .progress = completed ? std::optional<double>(1.0)
                                  : std::optional<double>(progressValue()),
            .error = error,
        };
    }

    void BackgroundAutosaveJob::initializeFromSession(
        const cupuacu::DocumentSession &session)
    {
        const auto lease = session.document.acquireReadLease();
        sampleFormat = static_cast<int>(lease.getSampleFormat());
        sampleRate = lease.getSampleRate();
        channelCount = lease.getChannelCount();
        frameCount = lease.getFrameCount();
        markers.clear();
        markers.reserve(lease.getMarkers().size());
        for (const auto &marker : lease.getMarkers())
        {
            markers.push_back(
                {.id = marker.id, .frame = marker.frame, .label = marker.label});
        }

        cupuacu::file::ensureParentDirectoryExists(path);
        temporaryPath = cupuacu::file::makeTemporarySiblingPath(path);
        output.open(temporaryPath, std::ios::binary);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to open autosave snapshot");
        }

        output.write(kAutosaveMagic, sizeof(kAutosaveMagic));
        writeU32(output, kAutosaveVersion);
        writeU32(output, static_cast<uint32_t>(sampleFormat));
        writeU32(output, static_cast<uint32_t>(sampleRate));
        writeI64(output, channelCount);
        writeI64(output, frameCount);
        writeString(output, currentFile);

        writeI64(output, static_cast<int64_t>(markers.size()));
        for (const auto &marker : markers)
        {
            writeU64(output, marker.id);
            writeI64(output, marker.frame);
            writeString(output, marker.label);
        }

        if (!output.good())
        {
            throw std::runtime_error("Failed to write autosave snapshot");
        }

        initialized = true;
    }

    void BackgroundAutosaveJob::writeFrameChunk(
        const cupuacu::Document::ReadLease &lease, const int64_t frameStart,
        const int64_t frameCountToWrite)
    {
        const auto frameEnd =
            std::min(frameCount, frameStart + frameCountToWrite);
        for (int64_t frame = frameStart; frame < frameEnd; ++frame)
        {
            for (int64_t channel = 0; channel < channelCount; ++channel)
            {
                writeFloat(output, lease.getSample(channel, frame));
            }
        }
        nextFrameToWrite = frameEnd;
        if (!output.good())
        {
            throw std::runtime_error("Failed to write autosave snapshot");
        }
    }

    void BackgroundAutosaveJob::finish()
    {
        output.close();
        cupuacu::file::replaceFile(temporaryPath, path);
        success = true;
        completed = true;
    }

    void BackgroundAutosaveJob::fail(std::string message)
    {
        error = std::move(message);
        success = false;
        completed = true;
        output.close();
        if (!temporaryPath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(temporaryPath, ec);
        }
    }

    double BackgroundAutosaveJob::progressValue() const
    {
        if (frameCount <= 0)
        {
            return 1.0;
        }
        return std::clamp(static_cast<double>(nextFrameToWrite) /
                              static_cast<double>(frameCount),
                          0.0, 1.0);
    }

    void BackgroundAutosaveJob::pump(cupuacu::State *state)
    {
        if (completed || !state)
        {
            return;
        }

        const int tabIndex = findTabIndexById(state, tabId);
        if (tabIndex < 0)
        {
            fail("Autosave tab no longer exists");
            return;
        }

        auto &session = state->tabs[static_cast<std::size_t>(tabIndex)].session;
        auto &document = session.document;
        if (session.autosaveSnapshotPath != path)
        {
            fail("Autosave snapshot path changed");
            return;
        }
        if (session.currentFile != currentFile)
        {
            fail("Autosave source file changed");
            return;
        }
        if (document.getWaveformDataVersion() != waveformDataVersion ||
            document.getMarkerDataVersion() != markerDataVersion)
        {
            completed = true;
            success = false;
            error = "stale";
            output.close();
            if (!temporaryPath.empty())
            {
                std::error_code ec;
                std::filesystem::remove(temporaryPath, ec);
            }
            return;
        }

        try
        {
            if (!initialized)
            {
                initializeFromSession(session);
            }

            const auto pumpStartedAt = std::chrono::steady_clock::now();
            while (nextFrameToWrite < frameCount)
            {
                const auto lease = document.acquireReadLease();
                writeFrameChunk(lease, nextFrameToWrite, kAutosaveFramesPerChunk);
                if (std::chrono::steady_clock::now() - pumpStartedAt >=
                    kAutosavePumpBudget)
                {
                    break;
                }
            }

            if (nextFrameToWrite >= frameCount)
            {
                finish();
            }
        }
        catch (const std::exception &e)
        {
            fail(e.what());
        }
        catch (...)
        {
            fail("An unknown error occurred.");
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
                    session.currentFile),
                cupuacu::destroyBackgroundAutosaveJob};
        }
    }

    void processPendingAutosaveWork(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        if (state->backgroundAutosaveJob && canRunAutosavePump(state))
        {
            state->backgroundAutosaveJob->pump(state);
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
                        if (session.autosaveSnapshotPath == snapshot.path)
                        {
                            session.autosavedWaveformDataVersion =
                                snapshot.waveformDataVersion;
                            session.autosavedMarkerDataVersion =
                                snapshot.markerDataVersion;
                            persistSessionState(state);
                        }
                    }
                }

                state->backgroundAutosaveJob.reset();
            }
        }

        if (state->backgroundAutosaveJob || !canRunAutosavePump(state))
        {
            return;
        }

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
                    tab.session.currentFile),
                cupuacu::destroyBackgroundAutosaveJob};
            break;
        }
    }
} // namespace cupuacu::actions::io
