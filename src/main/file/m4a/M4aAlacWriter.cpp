#include "M4aAlacWriter.hpp"

#include "../FileIo.hpp"
#include "../SampleQuantization.hpp"
#include "M4aAtoms.hpp"

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
        [[nodiscard]] std::uint32_t bitDepthForDocument(
            const cupuacu::Document::ReadLease &document)
        {
            switch (document.getSampleFormat())
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

        [[nodiscard]] cupuacu::SampleFormat sampleFormatForBitDepth(
            const std::uint32_t bitsPerSample)
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

        std::vector<std::uint8_t>
        makeInterleavedPcm(const cupuacu::Document::ReadLease &document,
                           const std::uint32_t bitsPerSample)
        {
            const auto channels = document.getChannelCount();
            const auto frames = document.getFrameCount();
            if (channels <= 0 || frames < 0 ||
                channels > static_cast<int64_t>(alac::maxChannels()) ||
                frames > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument(
                    "Document format cannot be exported as M4A ALAC");
            }

            std::vector<std::uint8_t> pcm;
            pcm.reserve(static_cast<std::size_t>(frames) *
                        static_cast<std::size_t>(channels) *
                        static_cast<std::size_t>((bitsPerSample + 7u) / 8u));
            for (int64_t frame = 0; frame < frames; ++frame)
            {
                for (int64_t channel = 0; channel < channels; ++channel)
                {
                    const auto quantized =
                        cupuacu::file::quantizeIntegerPcmSample(
                            sampleFormatForBitDepth(bitsPerSample),
                            document.getSample(channel, frame), false);
                    appendNativePcm(pcm, quantized, bitsPerSample);
                }
            }
            return pcm;
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
        if (outputPath.empty())
        {
            throw std::invalid_argument("Output path is empty");
        }
        if (document.getSampleRate() <= 0)
        {
            throw std::invalid_argument(
                "Document sample rate cannot be exported as M4A ALAC");
        }

        const auto bitDepth =
            requestedBitDepth == 0 ? bitDepthForDocument(document)
                                   : requestedBitDepth;
        if (progress)
        {
            progress(outputPath.string() + " (preparing PCM)", 0.0);
        }
        const auto pcm = makeInterleavedPcm(document, bitDepth);
        if (progress)
        {
            progress(outputPath.string() + " (encoding ALAC)", 0.25);
        }
        const auto encoded = alac::encodePcmPackets(
            {
                .sampleRate =
                    static_cast<std::uint32_t>(document.getSampleRate()),
                .channels =
                    static_cast<std::uint32_t>(document.getChannelCount()),
                .bitsPerSample = bitDepth,
                .framesPerPacket = alac::defaultFramesPerPacket(),
            },
            pcm);
        if (!encoded.has_value())
        {
            throw std::runtime_error("Failed to encode ALAC packets");
        }

        if (progress)
        {
            progress(outputPath.string() + " (assembling M4A)", 0.85);
        }
        const auto bytes = assembleAlacM4a(*encoded, document.getMarkers());
        cupuacu::file::writeFileAtomically(
            outputPath,
            [&bytes, &outputPath, &progress](
                const std::filesystem::path &temporaryPath)
            {
                if (progress)
                {
                    progress(outputPath.string() + " (writing file)", 0.95);
                }
                std::ofstream output(temporaryPath, std::ios::binary);
                if (!output)
                {
                    throw std::runtime_error("Failed to open M4A output file");
                }
                output.write(reinterpret_cast<const char *>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
                if (!output)
                {
                    throw std::runtime_error("Failed to write M4A output file");
                }
            });
        if (progress)
        {
            progress(outputPath.string(), 1.0);
        }
    }
} // namespace cupuacu::file::m4a
