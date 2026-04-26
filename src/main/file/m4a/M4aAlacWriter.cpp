#include "M4aAlacWriter.hpp"

#include "../FileIo.hpp"
#include "../SampleQuantization.hpp"
#include "M4aAtoms.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cupuacu::file::m4a
{
    namespace
    {
        constexpr std::uint32_t kAlacBitDepth = 16;

        void appendNativeI16(std::vector<std::uint8_t> &bytes,
                             const std::int16_t value)
        {
            const auto *raw = reinterpret_cast<const std::uint8_t *>(&value);
            bytes.push_back(raw[0]);
            bytes.push_back(raw[1]);
        }

        std::vector<std::uint8_t>
        makeInterleavedPcm16(const cupuacu::Document &document)
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
                        sizeof(std::int16_t));
            for (int64_t frame = 0; frame < frames; ++frame)
            {
                for (int64_t channel = 0; channel < channels; ++channel)
                {
                    const auto quantized =
                        cupuacu::file::quantizeIntegerPcmSample(
                            cupuacu::SampleFormat::PCM_S16,
                            document.getSample(channel, frame), false);
                    appendNativeI16(pcm, static_cast<std::int16_t>(quantized));
                }
            }
            return pcm;
        }
    } // namespace

    void writeAlacM4aFile(const cupuacu::Document &document,
                          const std::filesystem::path &outputPath)
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

        const auto pcm = makeInterleavedPcm16(document);
        const auto encoded = alac::encodePcmPackets(
            {
                .sampleRate =
                    static_cast<std::uint32_t>(document.getSampleRate()),
                .channels =
                    static_cast<std::uint32_t>(document.getChannelCount()),
                .bitsPerSample = kAlacBitDepth,
                .framesPerPacket = alac::defaultFramesPerPacket(),
            },
            pcm);
        if (!encoded.has_value())
        {
            throw std::runtime_error("Failed to encode ALAC packets");
        }

        const auto bytes = assembleAlacM4a(*encoded);
        cupuacu::file::writeFileAtomically(
            outputPath,
            [&bytes](const std::filesystem::path &temporaryPath)
            {
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
    }
} // namespace cupuacu::file::m4a
