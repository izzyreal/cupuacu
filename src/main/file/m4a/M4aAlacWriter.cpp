#include "M4aAlacWriter.hpp"

#include "../FileIo.hpp"
#include "../SampleQuantization.hpp"
#include "M4aAtoms.hpp"
#include "../../storage/DocumentAudioReader.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cupuacu::file::m4a
{
    namespace
    {
        [[nodiscard]] std::uint32_t
        bitDepthForDocument(const cupuacu::SampleFormat format)
        {
            switch (format)
            {
                case cupuacu::SampleFormat::PCM_S24:
                case cupuacu::SampleFormat::FLOAT32:
                case cupuacu::SampleFormat::FLOAT64:
                    return 24;
                case cupuacu::SampleFormat::PCM_S32:
                    return 32;
                case cupuacu::SampleFormat::PCM_S8:
                case cupuacu::SampleFormat::PCM_S16:
                case cupuacu::SampleFormat::Unknown:
                default:
                    return 16;
            }
        }

        [[nodiscard]] bool nativeLittleEndian()
        {
            const std::uint16_t value = 1;
            return *reinterpret_cast<const std::uint8_t *>(&value) == 1;
        }

        void appendNativePcm(std::vector<std::uint8_t> &bytes,
                             const std::int64_t value,
                             const std::uint32_t bitsPerSample)
        {
            const auto pcm = static_cast<std::int32_t>(value);
            const auto *raw = reinterpret_cast<const std::uint8_t *>(&pcm);
            switch (bitsPerSample)
            {
                case 16:
                    bytes.push_back(raw[0]);
                    bytes.push_back(raw[1]);
                    break;
                case 24:
                    if (nativeLittleEndian())
                    {
                        bytes.push_back(raw[0]);
                        bytes.push_back(raw[1]);
                        bytes.push_back(raw[2]);
                    }
                    else
                    {
                        bytes.push_back(raw[1]);
                        bytes.push_back(raw[2]);
                        bytes.push_back(raw[3]);
                    }
                    break;
                case 32:
                    bytes.push_back(raw[0]);
                    bytes.push_back(raw[1]);
                    bytes.push_back(raw[2]);
                    bytes.push_back(raw[3]);
                    break;
                default:
                    throw std::invalid_argument(
                        "Unsupported ALAC bit depth for M4A export");
            }
        }

        [[nodiscard]] cupuacu::SampleFormat
        sampleFormatForBitDepth(const std::uint32_t bitsPerSample)
        {
            switch (bitsPerSample)
            {
                case 16:
                    return cupuacu::SampleFormat::PCM_S16;
                case 24:
                    return cupuacu::SampleFormat::PCM_S24;
                case 32:
                    return cupuacu::SampleFormat::PCM_S32;
                default:
                    throw std::invalid_argument(
                        "Unsupported ALAC bit depth for M4A export");
            }
        }

    } // namespace

    void writeAlacM4aFile(const cupuacu::Document &document,
                          const std::filesystem::path &outputPath,
                          const std::uint32_t requestedBitDepth,
                          cupuacu::file::WriteProgressCallback progress)
    {
        const auto lease = document.acquireReadLease();
        writeAlacM4aFile(lease, outputPath, requestedBitDepth,
                         std::move(progress));
    }

    void writeAlacM4aFile(const cupuacu::Document::ReadLease &document,
                          const std::filesystem::path &outputPath,
                          const std::uint32_t requestedBitDepth,
                          cupuacu::file::WriteProgressCallback progress)
    {
        storage::DocumentAudioReader reader(document);
        writeAlacM4aFile(reader, document.getMarkers(), outputPath,
                         requestedBitDepth, std::move(progress));
    }

    void writeAlacM4aFile(const storage::AudioReader &audio,
                          const std::vector<DocumentMarker> &markers,
                          const std::filesystem::path &outputPath,
                          const std::uint32_t requestedBitDepth,
                          WriteProgressCallback progress)
    {
        const auto shape = audio.shape();
        if (outputPath.empty() || shape.sampleRate <= 0 ||
            shape.channels <= 0 || shape.channels > int(alac::maxChannels()) ||
            shape.frames <= 0 ||
            std::uint64_t(shape.frames) >
                std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument(
                "Document exceeds supported M4A ALAC format limits");
        }
        const auto bitDepth = requestedBitDepth
                                  ? requestedBitDepth
                                  : bitDepthForDocument(shape.format);
        const auto format = sampleFormatForBitDepth(bitDepth);
        const auto packetFrames = alac::defaultFramesPerPacket();
        const auto detail = outputPath.string();
        if (progress)
        {
            progress(detail, 0.0);
        }
        writeFileAtomically(
            outputPath,
            [&](const std::filesystem::path &temporaryPath)
            {
                std::ofstream output(temporaryPath, std::ios::binary);
                if (!output)
                {
                    throw std::runtime_error("Failed to open M4A output file");
                }
                beginAlacM4a(output);
                AlacMovieDescription description;
                description.packetSizes.reserve(
                    std::uint64_t(shape.frames) / packetFrames + 1);
                std::vector<float> samples(std::size_t(packetFrames) *
                                           shape.channels);
                std::vector<std::uint8_t> pcm;
                pcm.reserve(std::size_t(packetFrames) * shape.channels *
                            (bitDepth / 8));
                std::uint64_t audioBytes = 0;
                const auto encoded = alac::streamEncodedPcmPackets(
                    {std::uint32_t(shape.sampleRate),
                     std::uint32_t(shape.channels), bitDepth, packetFrames},
                    shape.frames,
                    [&](std::uint64_t start, std::uint32_t count,
                        std::span<std::uint8_t> destination)
                    {
                        // Progress callbacks also check cancellation between
                        // packets.
                        if (progress)
                        {
                            progress(detail, 0.99 * double(start) /
                                                 double(shape.frames));
                        }
                        for (int ch = 0; ch < shape.channels; ++ch)
                        {
                            audio.readChannel(
                                ch, static_cast<int64_t>(start),
                                {samples.data() +
                                     std::size_t(ch) * packetFrames,
                                 count});
                        }
                        pcm.clear();
                        for (std::uint32_t i = 0; i < count; ++i)
                        {
                            for (int ch = 0; ch < shape.channels; ++ch)
                            {
                                appendNativePcm(
                                    pcm,
                                    quantizeIntegerPcmSample(
                                        format,
                                        samples[std::size_t(ch) * packetFrames +
                                                i],
                                        false),
                                    bitDepth);
                            }
                        }
                        std::copy(pcm.begin(), pcm.end(), destination.begin());
                        return true;
                    },
                    [&](std::span<const std::uint8_t> packet)
                    {
                        output.write(
                            reinterpret_cast<const char *>(packet.data()),
                            static_cast<std::streamsize>(packet.size()));
                        if (!output)
                        {
                            throw std::runtime_error(
                                "Failed to write ALAC packet");
                        }
                        description.packetSizes.push_back(
                            static_cast<std::uint32_t>(packet.size()));
                        audioBytes += packet.size();
                        return true;
                    });
                if (!encoded)
                {
                    throw std::runtime_error("Failed to encode ALAC packets");
                }
                description.sampleRate = encoded->cookie.sampleRate;
                description.frameCount = encoded->frameCount;
                description.framesPerPacket = encoded->framesPerPacket;
                description.sampleEntry = {
                    encoded->cookie.channels, encoded->cookie.bitDepth,
                    encoded->cookie.sampleRate, encoded->cookie.bytes};
                // Cancellation here leaves the original destination untouched.
                if (progress)
                {
                    progress(detail + " (finalizing M4A)", 0.999);
                }
                finishAlacM4a(output, std::move(description), audioBytes,
                              markers);
                output.close();
                if (!output)
                {
                    throw std::runtime_error("Failed to close M4A output file");
                }
                if (progress)
                {
                    progress(detail, 1.0);
                }
            });
    }
} // namespace cupuacu::file::m4a
