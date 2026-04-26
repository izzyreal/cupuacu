#include "persistence/DocumentAutosave.hpp"

#include "file/FileIo.hpp"

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
        constexpr uint32_t kVersion = 1;

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

        float readFloat(std::istream &input)
        {
            const uint32_t bits = readU32(input);
            float value = 0.0f;
            static_assert(sizeof(value) == sizeof(bits));
            std::memcpy(&value, &bits, sizeof(value));
            return value;
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

            for (int64_t frame = 0; frame < document.getFrameCount(); ++frame)
            {
                for (int64_t channel = 0; channel < document.getChannelCount();
                     ++channel)
                {
                    writeFloat(output, document.getSample(channel, frame));
                }
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
        catch (...)
        {
            return false;
        }
    }

    bool loadDocumentAutosaveSnapshot(const std::filesystem::path &path,
                                      cupuacu::DocumentSession &session)
    {
        if (path.empty())
        {
            return false;
        }

        try
        {
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

            cupuacu::Document document;
            document.initialize(format, sampleRate, static_cast<uint32_t>(channels),
                                frames);
            for (int64_t frame = 0; frame < frames; ++frame)
            {
                for (int64_t channel = 0; channel < channels; ++channel)
                {
                    document.setSample(channel, frame, readFloat(input), false);
                }
            }
            document.replaceMarkers(std::move(markers));

            session.clearCurrentFile();
            if (!currentFile.empty())
            {
                session.setCurrentFile(currentFile);
            }
            session.document = std::move(document);
            session.autosaveSnapshotPath = path;
            session.autosavedWaveformDataVersion =
                session.document.getWaveformDataVersion();
            session.autosavedMarkerDataVersion =
                session.document.getMarkerDataVersion();
            session.syncSelectionAndCursorToDocumentLength();
            return true;
        }
        catch (...)
        {
            return false;
        }
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
} // namespace cupuacu::persistence
