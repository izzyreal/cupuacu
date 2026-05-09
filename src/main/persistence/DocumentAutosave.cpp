#include "persistence/DocumentAutosave.hpp"

#include "LongTask.hpp"
#include "Logger.hpp"
#include "file/FileIo.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr char kMagic[] = "CUPUACU_AUTOSAVE";
        constexpr uint32_t kVersion = 2;
        constexpr int64_t kAudioBlockFrames = 16384;

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

        std::string readString(std::istream &input)
        {
            const uint32_t size = readU32(input);
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
                    level[static_cast<std::size_t>(peakIndex)] = {
                        .min = readFloat(input),
                        .max = readFloat(input),
                    };
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
                for (int64_t frame = 0; frame < framesToWrite; ++frame)
                {
                    for (int64_t channel = 0; channel < channelCount; ++channel)
                    {
                        interleaved[static_cast<std::size_t>(frame) *
                                        static_cast<std::size_t>(channelCount) +
                                    static_cast<std::size_t>(channel)] =
                            lease.getSample(channel, frameStart + frame);
                    }
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
        const DocumentAutosaveLoadCancelCheck &isCanceled)
    {
        if (path.empty())
        {
            return false;
        }

        try
        {
            const auto startedAt = std::chrono::steady_clock::now();
            std::ifstream input(path, std::ios::binary);
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

            const auto format = sampleFormatFromInt(readU32(input));
            const auto sampleRate = readU32(input);
            const auto channels = readI64(input);
            const auto frames = readI64(input);
            if (channels < 0 || frames < 0 ||
                channels > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }

            const auto currentFile = readString(input);
            std::vector<cupuacu::DocumentMarker> markers;
            const auto markerCount = readI64(input);
            if (markerCount < 0)
            {
                return false;
            }
            markers.reserve(static_cast<std::size_t>(markerCount));
            for (int64_t index = 0; index < markerCount; ++index)
            {
                markers.push_back(cupuacu::DocumentMarker{
                    .id = readU64(input),
                    .frame = readI64(input),
                    .label = readString(input),
                });
            }

            const auto waveformCacheChannelCount = readI64(input);
            if (waveformCacheChannelCount != channels)
            {
                return false;
            }
            std::vector<gui::WaveformCache::BuildResult> waveformCacheResults;
            waveformCacheResults.reserve(
                static_cast<std::size_t>(waveformCacheChannelCount));
            for (int64_t channel = 0; channel < waveformCacheChannelCount;
                 ++channel)
            {
                waveformCacheResults.push_back(readWaveformCacheResult(input));
            }
            const auto parsedMetadataAt = std::chrono::steady_clock::now();

            cupuacu::Document document;
            document.initialize(format, sampleRate, static_cast<uint32_t>(channels),
                                frames);
            if (progress)
            {
                progress(frames > 0 ? std::optional<double>(0.0)
                                    : std::optional<double>(1.0));
            }
            std::vector<float> interleaved(
                static_cast<std::size_t>(std::max<int64_t>(1, channels)) *
                static_cast<std::size_t>(kAudioBlockFrames));
            for (int64_t frameStart = 0; frameStart < frames;
                 frameStart += kAudioBlockFrames)
            {
                if (isCanceled && isCanceled())
                {
                    throw cupuacu::LongTaskCanceledError{};
                }
                const auto framesToRead = std::min<int64_t>(
                    kAudioBlockFrames, frames - frameStart);
                const auto sampleCount = framesToRead * channels;
                readFloatBlock(input, interleaved.data(),
                               static_cast<std::size_t>(sampleCount));

                document.writeInterleavedFloatBlock(frameStart, interleaved.data(),
                                                    framesToRead, channels, false);

                if (progress)
                {
                    progress(static_cast<double>(frameStart + framesToRead) /
                             static_cast<double>(frames));
                }
            }
            const auto loadedAudioAt = std::chrono::steady_clock::now();
            document.replaceMarkers(std::move(markers));

            session.clearCurrentFile();
            if (!currentFile.empty())
            {
                session.setCurrentFile(currentFile);
            }
            session.document = std::move(document);
            session.waveformCaches.resetToChannelCount(channels);
            for (int64_t channel = 0; channel < channels; ++channel)
            {
                session.getWaveformCache(static_cast<int>(channel))
                    .applyBuildResult(std::move(
                        waveformCacheResults[static_cast<std::size_t>(channel)]));
            }
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
                               const cupuacu::Document &clipboard)
    {
        if (path.empty() || clipboard.getChannelCount() <= 0)
        {
            return false;
        }

        cupuacu::DocumentSession session;
        session.document = clipboard;
        session.clearCurrentFile();
        return saveDocumentAutosaveSnapshot(path, session);
    }

    bool loadClipboardSnapshot(const std::filesystem::path &path,
                               cupuacu::Document &clipboard)
    {
        cupuacu::DocumentSession session;
        if (!loadDocumentAutosaveSnapshot(path, session))
        {
            return false;
        }
        clipboard = std::move(session.document);
        return clipboard.getChannelCount() > 0;
    }

    void removeDocumentAutosaveSnapshot(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return;
        }

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void removeClipboardSnapshot(const std::filesystem::path &path)
    {
        removeDocumentAutosaveSnapshot(path);
    }
} // namespace cupuacu::persistence
