#include "waveform/StreamingPeakBuilder.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "RevisionPersistence.hpp"
#include "LegacyRecovery.hpp"
#include "../waveform/DecodedWaveformBuilder.hpp"
#include "../concurrency/DeferredRelease.hpp"
#include "../concurrency/TaskScheduler.hpp"

#include "LongTask.hpp"
#include "Logger.hpp"
#include "file/FileIo.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr char kMagic[] = "CUPUACU_AUTOSAVE";
        constexpr uint32_t kVersion = 2;
        constexpr int64_t kAudioBlockFrames = 16384;

        class ClipboardSnapshotWorker
        {
        public:
            ClipboardSnapshotWorker()
                : scheduler(concurrency::defaultTaskScheduler())
            {
            }
            ~ClipboardSnapshotWorker()
            {
                flush();
            }

            void schedule(std::filesystem::path path,
                          cupuacu::ClipboardAudio clipboard)
            {
                {
                    std::lock_guard lock(mutex);
                    const auto revision = clipboard.getRevision();
                    if ((pending.has_value() &&
                         pending->path == path &&
                         pending->revision == revision) ||
                        (!pending.has_value() && busy &&
                         activePath == path &&
                         activeRevision == revision) ||
                        (!pending.has_value() && !busy &&
                         completedPath == path &&
                         completedRevision == revision))
                    {
                        return;
                    }
                    pending = Request{std::move(path), std::move(clipboard),
                                      revision};
                    if (!busy)
                    {
                        dispatch();
                    }
                }
            }

            void flush()
            {
                std::unique_lock lock(mutex);
                cv.wait(lock,
                        [this]
                        { return !busy && !pending.has_value(); });
            }

        private:
            struct Request
            {
                std::filesystem::path path;
                cupuacu::ClipboardAudio clipboard;
                uint64_t revision = 0;
            };

            std::mutex mutex;
            std::condition_variable cv;
            std::optional<Request> pending;
            bool busy = false;
            std::filesystem::path activePath;
            uint64_t activeRevision = 0;
            std::filesystem::path completedPath;
            uint64_t completedRevision = 0;
            std::shared_ptr<concurrency::TaskScheduler> scheduler;
            concurrency::TaskScheduler::Ticket ticket;
            void dispatch()
            {
                busy = true;
                try
                {
                    ticket = scheduler->submit(
                        [this]
                        {
                            run();
                        },
                        {.priority =
                             concurrency::TaskScheduler::Priority::Autosave,
                         .deadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(5)});
                }
                catch (const std::exception &e)
                {
                    busy = false;
                    pending.reset();
                    logging::info(std::string("Clipboard autosave: ") +
                                  e.what());
                    cv.notify_all();
                }
            }

            void run()
            {
                std::optional<Request> request;
                {
                    std::lock_guard lock(mutex);
                    request = std::move(pending);
                    pending.reset();
                    activePath = request->path;
                    activeRevision = request->revision;
                }
                bool saved = false;
                try
                {
                    if (request->clipboard.hasAudio())
                        saved = saveClipboardSnapshot(request->path,
                                                      request->clipboard);
                    else
                    {
                        removeClipboardSnapshot(request->path);
                        saved = true;
                    }
                }
                catch (const std::exception &e)
                {
                    logging::info(std::string("Clipboard autosave: ") +
                                  e.what());
                }
                request.reset(); // Release completed sources on this worker.
                std::lock_guard lock(mutex);
                busy = false;
                completedPath =
                    saved ? std::move(activePath) : std::filesystem::path{};
                completedRevision = saved ? activeRevision : 0;
                activeRevision = 0;
                if (pending)
                {
                    dispatch();
                }
                cv.notify_all();
            }
        };

        ClipboardSnapshotWorker &clipboardSnapshotWorker()
        {
            static ClipboardSnapshotWorker worker;
            return worker;
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

        uint32_t readU32(std::istream &input)
        {
            unsigned char bytes[4]{};
            input.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
            if (!input)
            {
                throw std::runtime_error("Truncated autosave snapshot");
            }
            return static_cast<uint32_t>(bytes[0]) |
                   (static_cast<uint32_t>(bytes[1]) << 8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) |
                   (static_cast<uint32_t>(bytes[3]) << 24);
        }

        uint64_t readU64(std::istream &input)
        {
            uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 8)
            {
                const int byte = input.get();
                if (byte == std::char_traits<char>::eof())
                {
                    throw std::runtime_error("Truncated autosave snapshot");
                }
                value |= static_cast<uint64_t>(
                             static_cast<unsigned char>(byte))
                         << shift;
            }
            return value;
        }

        int64_t readI64(std::istream &input)
        {
            return static_cast<int64_t>(readU64(input));
        }

        void writeString(std::ostream &output, const std::string &value)
        {
            if (value.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::runtime_error("Autosave string is too large");
            }
            writeU32(output, static_cast<uint32_t>(value.size()));
            output.write(value.data(), static_cast<std::streamsize>(value.size()));
        }

        std::string readString(std::istream &input, uint64_t fileBytes)
        {
            const uint32_t size = readU32(input);
            const auto at = input.tellg();
            if (at < 0 || uint64_t(at) > fileBytes ||
                size > fileBytes - uint64_t(at))
            {
                throw std::runtime_error("Truncated autosave string");
            }
            std::string value(size, '\0');
            input.read(value.data(), static_cast<std::streamsize>(value.size()));
            if (!input)
            {
                throw std::runtime_error("Truncated autosave snapshot");
            }
            return value;
        }

        void writeFloat(std::ostream &output, const float value)
        {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(output, bits);
        }

        void writeFloatBlock(std::ostream &output, const float *values,
                             const std::size_t sampleCount)
        {
            if (sampleCount == 0)
            {
                return;
            }
            output.write(reinterpret_cast<const char *>(values),
                         static_cast<std::streamsize>(sampleCount * sizeof(float)));
            if (!output)
            {
                throw std::runtime_error("Failed to write autosave snapshot");
            }
        }

        float readFloat(std::istream &input)
        {
            const uint32_t bits = readU32(input);
            float value = 0.0f;
            static_assert(sizeof(value) == sizeof(bits));
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        void readFloatBlock(std::istream &input, float *values,
                            const std::size_t sampleCount)
        {
            if (sampleCount == 0)
            {
                return;
            }
            input.read(reinterpret_cast<char *>(values),
                       static_cast<std::streamsize>(sampleCount * sizeof(float)));
            if (!input)
            {
                throw std::runtime_error("Truncated autosave snapshot");
            }
        }

        void writeWaveformCacheState(
            std::ostream &output, const gui::WaveformCache::BuildState &state)
        {
            writeI64(output, state.numSamples);
            writeI64(output, state.dirtyFromBlock);
            writeI64(output, state.dirtyToBlock);
            writeU32(output, static_cast<uint32_t>(state.levels.size()));
            for (const auto &level : state.levels)
            {
                if (level.size() > std::numeric_limits<uint32_t>::max())
                {
                    throw std::runtime_error(
                        "Autosave waveform cache level is too large");
                }
                writeU32(output, static_cast<uint32_t>(level.size()));
                for (const auto &peak : level)
                {
                    writeFloat(output, peak.min);
                    writeFloat(output, peak.max);
                }
            }
        }

        gui::WaveformCache::BuildResult readWaveformCacheResult(
            std::istream &input)
        {
            gui::WaveformCache::BuildResult result;
            result.numSamples = readI64(input);
            result.dirtyFromBlock = readI64(input);
            result.dirtyToBlock = readI64(input);
            const auto levelCount = readU32(input);
            result.levels.resize(levelCount);
            for (uint32_t levelIndex = 0; levelIndex < levelCount; ++levelIndex)
            {
                const auto peakCount = readU32(input);
                auto &level = result.levels[static_cast<std::size_t>(levelIndex)];
                level.resize(peakCount);
                for (uint32_t peakIndex = 0; peakIndex < peakCount; ++peakIndex)
                {
                    level.set(static_cast<std::size_t>(peakIndex),
                              {
                                  .min = readFloat(input),
                                  .max = readFloat(input),
                              });
                }
            }
            return result;
        }

        cupuacu::SampleFormat sampleFormatFromInt(const uint32_t value)
        {
            switch (static_cast<cupuacu::SampleFormat>(value))
            {
                case cupuacu::SampleFormat::PCM_S8:
                case cupuacu::SampleFormat::PCM_S16:
                case cupuacu::SampleFormat::PCM_S24:
                case cupuacu::SampleFormat::PCM_S32:
                case cupuacu::SampleFormat::FLOAT32:
                case cupuacu::SampleFormat::FLOAT64:
                case cupuacu::SampleFormat::Unknown:
                    return static_cast<cupuacu::SampleFormat>(value);
            }
            return cupuacu::SampleFormat::Unknown;
        }

        void writeSnapshotFile(const std::filesystem::path &path,
                               const cupuacu::DocumentSession &session)
        {
            std::ofstream output(path, std::ios::binary);
            CUPUACU_METRIC(
                auto ioObservation = performance::observeWrite(
                    output, performance::Work::AutosaveBytesWritten));
            if (!output.is_open())
            {
                throw std::runtime_error("Failed to open autosave snapshot");
            }

            const auto &document = session.document;
            output.write(kMagic, sizeof(kMagic));
            writeU32(output, kVersion);
            writeU32(output, static_cast<uint32_t>(document.getSampleFormat()));
            writeU32(output, static_cast<uint32_t>(document.getSampleRate()));
            writeI64(output, document.getChannelCount());
            writeI64(output, document.getFrameCount());
            writeString(output, session.currentFile);

            const auto &markers = document.getMarkers();
            writeI64(output, static_cast<int64_t>(markers.size()));
            for (const auto &marker : markers)
            {
                writeU64(output, marker.id);
                writeI64(output, marker.frame);
                writeString(output, marker.label);
            }

            writeI64(output, document.getChannelCount());
            for (int64_t channel = 0; channel < document.getChannelCount();
                 ++channel)
            {
                writeWaveformCacheState(
                    output, session.getWaveformCache(static_cast<int>(channel))
                                .snapshotBuildState());
            }

            const auto lease = document.acquireReadLease();
            const int64_t channelCount = lease.getChannelCount();
            std::vector<float> interleaved(
                static_cast<std::size_t>(std::max<int64_t>(1, channelCount)) *
                static_cast<std::size_t>(kAudioBlockFrames));
            for (int64_t frameStart = 0; frameStart < lease.getFrameCount();
                 frameStart += kAudioBlockFrames)
            {
                const auto framesToWrite = std::min<int64_t>(
                    kAudioBlockFrames, lease.getFrameCount() - frameStart);
                for (int64_t channel = 0; channel < channelCount; ++channel)
                {
                    lease.readChannelFloatBlock(channel, frameStart,
                                                interleaved.data() + channel,
                                                framesToWrite, channelCount);
                }

                const auto sampleCount = framesToWrite * channelCount;
                writeFloatBlock(output, interleaved.data(),
                                static_cast<std::size_t>(sampleCount));
            }

            if (!output.good())
            {
                throw std::runtime_error("Failed to write autosave snapshot");
            }
        }
    } // namespace

    bool saveDocumentAutosaveSnapshot(const std::filesystem::path &path,
                                      const cupuacu::DocumentSession &session)
    {
        if (path.empty() || session.document.getChannelCount() <= 0)
        {
            return false;
        }

        try
        {
            if (session.hasReadRevision())
            {
                RevisionPersistence::save(
                    path, *RevisionPersistence::capture(session));
                return true;
            }
            cupuacu::file::writeFileAtomically(
                path,
                [&](const std::filesystem::path &temporaryPath)
                {
                    writeSnapshotFile(temporaryPath, session);
                });
            return true;
        }
        catch (const std::exception &e)
        {
            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    bool loadDocumentAutosaveSnapshot(const std::filesystem::path &path,
                                      cupuacu::DocumentSession &session)
    {
        return loadDocumentAutosaveSnapshot(path, session, {}, {});
    }

    bool loadDocumentAutosaveSnapshot(
        const std::filesystem::path &path, cupuacu::DocumentSession &session,
        const DocumentAutosaveLoadProgress &progress)
    {
        return loadDocumentAutosaveSnapshot(path, session, progress, {});
    }

    bool loadDocumentAutosaveSnapshot(
        const std::filesystem::path &path, cupuacu::DocumentSession &session,
        const DocumentAutosaveLoadProgress &progress,
        const DocumentAutosaveLoadCancelCheck &isCanceled,
        const PersistedOpenDocumentState *legacyState)
    {
        if (path.empty())
        {
            return false;
        }

        try
        {
            if (storage::RevisionArchive::recognizes(path))
            {
                RevisionPersistence::load(path, session, isCanceled);
                if (progress)
                {
                    progress(1.0);
                }
                return true;
            }
            const auto startedAt = std::chrono::steady_clock::now();
            std::ifstream input(path, std::ios::binary);
            CUPUACU_METRIC(auto ioObservation = performance::observeRead(
                               input, performance::Work::AutosaveBytesRead));
            if (!input.is_open())
            {
                return false;
            }

            char magic[sizeof(kMagic)]{};
            input.read(magic, sizeof(magic));
            if (!input || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
            {
                return false;
            }
            if (readU32(input) != kVersion)
            {
                return false;
            }

            const auto fileBytes = std::filesystem::file_size(path);
            const auto fileTime = std::filesystem::last_write_time(path);
            const auto format = sampleFormatFromInt(readU32(input));
            const auto sampleRate = readU32(input);
            const auto channels = readI64(input);
            const auto frames = readI64(input);
            if (format == SampleFormat::Unknown || channels <= 0 ||
                channels > 256 || frames < 0 || sampleRate == 0 ||
                sampleRate > INT_MAX ||
                frames > INT64_MAX / channels / sizeof(float))
            {
                return false;
            }

            const auto currentFile = readString(input, fileBytes);
            std::vector<cupuacu::DocumentMarker> markers;
            const auto markerCount = readI64(input);
            if (markerCount < 0 || uint64_t(markerCount) > fileBytes / 20)
            {
                return false;
            }
            markers.reserve(static_cast<std::size_t>(markerCount));
            for (int64_t index = 0; index < markerCount; ++index)
            {
                if (isCanceled && isCanceled())
                {
                    throw LongTaskCanceledError{};
                }
                markers.push_back(cupuacu::DocumentMarker{
                    .id = readU64(input),
                    .frame = readI64(input),
                    .label = readString(input, fileBytes),
                });
            }

            const auto waveformCacheChannelCount = readI64(input);
            if (waveformCacheChannelCount != channels)
            {
                return false;
            }
            for (int64_t channel = 0; channel < channels; ++channel)
            {
                (void)readI64(input);
                (void)readI64(input);
                (void)readI64(input);
                const auto levels = readU32(input);
                if (levels > 64)
                {
                    throw std::runtime_error("Invalid legacy peak levels");
                }
                for (uint32_t level = 0; level < levels; ++level)
                {
                    if (isCanceled && isCanceled())
                    {
                        throw LongTaskCanceledError{};
                    }
                    const uint64_t bytes = uint64_t(readU32(input)) * 8;
                    const auto at = input.tellg();
                    if (at < 0 || uint64_t(at) > fileBytes ||
                        bytes > fileBytes - uint64_t(at))
                    {
                        throw std::runtime_error("Truncated legacy peaks");
                    }
                    input.seekg(std::streamoff(bytes), std::ios::cur);
                }
            }
            const auto audioStart = input.tellg();
            if (audioStart < 0 || uint64_t(audioStart) > fileBytes ||
                uint64_t(frames) * channels * sizeof(float) >
                    fileBytes - uint64_t(audioStart))
            {
                throw std::runtime_error("Truncated autosave samples");
            }
            const auto parsedMetadataAt = std::chrono::steady_clock::now();

            const storage::AudioShape shape{frames, int(channels),
                                            int(sampleRate), format};
            auto store = legacyRecoveryStore(path.parent_path());
            auto cache =
                storage::defaultDecodedBlockCache();
            waveform::StreamingPeakBuilder peaks(shape, cache, isCanceled);
            storage::AudioRevisionBuilder builder(
                shape, store, cache,
                [&](int64_t first, auto blocks, uint32_t count)
                {
                    peaks.appendFrom(
                        shape, first + count,
                        [&](int c, int64_t at, std::span<float> out)
                        {
                            std::copy_n(blocks[c].data() + at - first,
                                        out.size(), out.data());
                        });
                });
            if (progress)
            {
                progress(frames ? 0.0 : 1.0);
            }
            storage::WorkingVector<float, storage::MemoryUse::Import>
            interleaved(std::size_t(channels) * kAudioBlockFrames);
            for (int64_t first = 0; first < frames;)
            {
                if (isCanceled && isCanceled())
                {
                    throw LongTaskCanceledError{};
                }
                const auto count = std::min(kAudioBlockFrames, frames - first);
                readFloatBlock(input, interleaved.data(),
                               std::size_t(count * channels));
                builder.appendInterleaved(
                    std::span(interleaved).first(count * channels));
                first += count;
                if (progress)
                {
                    progress(double(first) / double(frames));
                }
            }
            auto audio = storage::AudioEditRevision::from(
                builder.finish({}, peaks.finish()));
            cupuacu::DocumentSession restored;
            restored.document.setExternalAudioShape(format, sampleRate,
                                                    channels, frames);
            restored.document.replaceMarkers(std::move(markers));
            if (!currentFile.empty())
            {
                restored.setCurrentFile(currentFile);
            }
            restored.bindReadRevision(std::move(audio));
            restored.autosaveSnapshotPath = path;
            const bool migrated = migrateLegacyHistory(
                restored, legacyState, path.parent_path(), isCanceled);
            if (!migrated)
            {
                throw std::runtime_error("Legacy history migration failed");
            }
            if (isCanceled && isCanceled())
            {
                throw LongTaskCanceledError{};
            }
            const auto loadedAudioAt = std::chrono::steady_clock::now();
            if (std::filesystem::file_size(path) != fileBytes ||
                std::filesystem::last_write_time(path) != fileTime)
            {
                throw std::runtime_error(
                    "Legacy snapshot changed during recovery");
            }
            // Publish the new format only after audio and the matching history
            // are durable. The old snapshot stays readable on cancellation or
            // write failure, and a second startup can use the archive directly.
            CUPUACU_METRIC(performance::add(
                performance::Work::AutosaveBytesRead, uint64_t(input.tellg())));
            input.close();
            if (progress)
            {
                progress(std::nullopt);
            }
            if (migrated)
            {
                RevisionPersistence::save(
                    path, *restored.recoveredRevisionCheckpoint,
                    [&]
                    {
                        if (isCanceled && isCanceled())
                        {
                            throw LongTaskCanceledError{};
                        }
                    },
                    UINT64_MAX, isCanceled);
            }
            if (progress)
            {
                progress(1.0);
            }
            if (legacyState && !legacyState->undoStorePath.empty() &&
                std::filesystem::is_directory(legacyState->undoStorePath))
            {
                restored.undoStore.attach(legacyState->undoStorePath);
            }
            if (restored.hasReadRevision())
            {
                restored.waveformCaches = {};
            }
            session = std::move(restored);
            const auto appliedStateAt = std::chrono::steady_clock::now();
            session.autosaveSnapshotPath = path;
            session.autosavedWaveformDataVersion =
                session.document.getWaveformDataVersion();
            session.autosavedMarkerDataVersion =
                session.document.getMarkerDataVersion();
            session.syncSelectionAndCursorToDocumentLength();
            const auto finishedAt = std::chrono::steady_clock::now();

            const auto metadataMs = std::chrono::duration_cast<
                std::chrono::milliseconds>(parsedMetadataAt - startedAt)
                                        .count();
            const auto audioMs = std::chrono::duration_cast<
                std::chrono::milliseconds>(loadedAudioAt - parsedMetadataAt)
                                     .count();
            const auto applyMs = std::chrono::duration_cast<
                std::chrono::milliseconds>(appliedStateAt - loadedAudioAt)
                                     .count();
            const auto finalizeMs = std::chrono::duration_cast<
                std::chrono::milliseconds>(finishedAt - appliedStateAt)
                                        .count();
            const auto totalMs = std::chrono::duration_cast<
                std::chrono::milliseconds>(finishedAt - startedAt)
                                     .count();
            cupuacu::logging::info(
                "Autosave snapshot load timings: path=" + path.string() +
                " frames=" + std::to_string(frames) +
                " channels=" + std::to_string(channels) +
                " metadata_ms=" + std::to_string(metadataMs) +
                " audio_ms=" + std::to_string(audioMs) +
                " apply_ms=" + std::to_string(applyMs) +
                " finalize_ms=" + std::to_string(finalizeMs) +
                " total_ms=" + std::to_string(totalMs));
            cupuacu::logging::flush();
            return true;
        }
        catch (const cupuacu::LongTaskCanceledError &)
        {
            throw;
        }
        catch (...)
        {
            return false;
        }
    }

    bool saveClipboardSnapshot(const std::filesystem::path &path,
                               const cupuacu::ClipboardAudio &clipboard)
    {
        if (path.empty() || !clipboard.hasAudio())
        {
            return false;
        }

        cupuacu::DocumentSession session;
        if (auto audio = clipboard.getAudioRevision())
        {
            const auto shape = audio->shape();
            session.document.setExternalAudioShape(
                shape.format, shape.sampleRate, shape.channels, shape.frames);
            session.bindReadRevision(audio);
            try
            {
                auto cp = RevisionPersistence::capture(session);
                cp->metadata["clipboard"] = true;
                RevisionPersistence::save(path, *cp);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            session.document = clipboard.toDocument();
        }
        return saveDocumentAutosaveSnapshot(path, session);
    }

    bool loadClipboardSnapshot(const std::filesystem::path &path,
                               cupuacu::ClipboardAudio &clipboard)
    {
        cupuacu::DocumentSession session;
        if (!loadDocumentAutosaveSnapshot(path, session))
        {
            return false;
        }
        if (session.hasReadRevision())
        {
            clipboard.assignRevision(session.getEditRevision());
        }
        else
        {
            clipboard.assignDocument(session.document);
        }
        return clipboard.hasAudio();
    }

    void scheduleClipboardSnapshot(const std::filesystem::path &path,
                                   cupuacu::ClipboardAudio clipboard)
    {
        if (path.empty())
        {
            return;
        }
        clipboardSnapshotWorker().schedule(path, std::move(clipboard));
    }

    void flushScheduledClipboardSnapshots()
    {
        clipboardSnapshotWorker().flush();
    }

    void removeDocumentAutosaveSnapshot(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return;
        }

        try
        {
            storage::RevisionArchive::remove(path);
        }
        catch (...)
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }

    void removeClipboardSnapshot(const std::filesystem::path &path)
    {
        removeDocumentAutosaveSnapshot(path);
    }
} // namespace cupuacu::persistence
