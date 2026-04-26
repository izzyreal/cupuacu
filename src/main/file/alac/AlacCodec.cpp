#include "AlacCodec.hpp"

#include "ALACAudioTypes.h"
#include "ALACBitUtilities.h"
#include "ALACDecoder.h"
#include "ALACEncoder.h"
#include "EndianPortable.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace cupuacu::file::alac
{
    namespace
    {
        [[nodiscard]] std::uint32_t alacFormatFlagForBits(
            const std::uint32_t bitsPerSample)
        {
            switch (bitsPerSample)
            {
                case 16:
                    return 1;
                case 20:
                    return 2;
                case 24:
                    return 3;
                case 32:
                    return 4;
                default:
                    return 0;
            }
        }

        [[nodiscard]] AudioFormatDescription makeAlacOutputFormat(
            const AlacEncodingParameters &parameters)
        {
            AudioFormatDescription format{};
            format.mSampleRate =
                static_cast<alac_float64_t>(parameters.sampleRate);
            format.mFormatID = kALACFormatAppleLossless;
            format.mFormatFlags =
                alacFormatFlagForBits(parameters.bitsPerSample);
            format.mFramesPerPacket = parameters.framesPerPacket;
            format.mChannelsPerFrame = parameters.channels;
            return format;
        }

        [[nodiscard]] AudioFormatDescription makeNativePcmInputFormat(
            const AlacEncodingParameters &parameters)
        {
            const auto nativeEndian =
                static_cast<std::uint32_t>(kALACFormatFlagsNativeEndian);
            const auto signedInteger =
                static_cast<std::uint32_t>(kALACFormatFlagIsSignedInteger);
            const auto packed =
                static_cast<std::uint32_t>(kALACFormatFlagIsPacked);

            AudioFormatDescription format{};
            format.mSampleRate =
                static_cast<alac_float64_t>(parameters.sampleRate);
            format.mFormatID = kALACFormatLinearPCM;
            format.mFormatFlags = nativeEndian | signedInteger | packed;
            format.mBytesPerPacket =
                parameters.channels * ((parameters.bitsPerSample + 7u) / 8u);
            format.mFramesPerPacket = 1;
            format.mBytesPerFrame = format.mBytesPerPacket;
            format.mChannelsPerFrame = parameters.channels;
            format.mBitsPerChannel = parameters.bitsPerSample;
            return format;
        }

        [[nodiscard]] std::optional<AlacEncoderCookie>
        encoderCookieFrom(ALACEncoder &encoder, const std::uint32_t channels)
        {
            const auto cookieSize = encoder.GetMagicCookieSize(channels);
            if (cookieSize == 0)
            {
                return std::nullopt;
            }

            AlacEncoderCookie cookie{};
            cookie.bytes.resize(cookieSize);
            auto writableCookieSize = cookieSize;
            encoder.GetMagicCookie(cookie.bytes.data(), &writableCookieSize);
            if (writableCookieSize == 0 ||
                writableCookieSize > cookie.bytes.size() ||
                writableCookieSize < sizeof(ALACSpecificConfig))
            {
                return std::nullopt;
            }
            cookie.bytes.resize(writableCookieSize);

            ALACSpecificConfig config{};
            std::memcpy(&config, cookie.bytes.data(), sizeof(config));
            cookie.frameLength = Swap32BtoN(config.frameLength);
            cookie.bitDepth = config.bitDepth;
            cookie.channels = config.numChannels;
            cookie.sampleRate = Swap32BtoN(config.sampleRate);
            return cookie;
        }

        [[nodiscard]] std::size_t maxOutputPacketBytes(
            const AlacEncodingParameters &parameters)
        {
            return static_cast<std::size_t>(parameters.framesPerPacket) *
                       static_cast<std::size_t>(parameters.channels) *
                       static_cast<std::size_t>((10u + 32u) / 8u) +
                   16u;
        }

        [[nodiscard]] std::uint32_t bytesPerSample(
            const std::uint32_t bitsPerSample)
        {
            return (bitsPerSample + 7u) / 8u;
        }

        [[nodiscard]] bool decoderConfigMatches(
            const ALACDecoder &decoder,
            const AlacDecodingParameters &parameters)
        {
            return decoder.mConfig.frameLength == parameters.framesPerPacket &&
                   decoder.mConfig.bitDepth == parameters.bitsPerSample &&
                   decoder.mConfig.numChannels == parameters.channels &&
                   decoder.mConfig.sampleRate == parameters.sampleRate;
        }

    } // namespace

    std::uint32_t defaultFramesPerPacket()
    {
        return kALACDefaultFramesPerPacket;
    }

    std::uint32_t maxChannels()
    {
        return kALACMaxChannels;
    }

    bool isSupportedEncoding(const AlacEncodingParameters &parameters)
    {
        return parameters.sampleRate > 0 && parameters.channels > 0 &&
               parameters.channels <= maxChannels() &&
               parameters.framesPerPacket == defaultFramesPerPacket() &&
               alacFormatFlagForBits(parameters.bitsPerSample) != 0;
    }

    std::optional<AlacEncoderCookie>
    makeEncoderCookie(AlacEncodingParameters parameters)
    {
        if (parameters.framesPerPacket == 0)
        {
            parameters.framesPerPacket = defaultFramesPerPacket();
        }
        if (!isSupportedEncoding(parameters))
        {
            return std::nullopt;
        }

        ALACEncoder encoder;
        encoder.SetFrameSize(parameters.framesPerPacket);
        const auto format = makeAlacOutputFormat(parameters);
        if (encoder.InitializeEncoder(format) != ALAC_noErr)
        {
            return std::nullopt;
        }

        return encoderCookieFrom(encoder, parameters.channels);
    }

    std::optional<AlacEncodedPackets>
    encodePcmPackets(AlacEncodingParameters parameters,
                     const std::vector<std::uint8_t> &interleavedPcmBytes)
    {
        if (parameters.framesPerPacket == 0)
        {
            parameters.framesPerPacket = defaultFramesPerPacket();
        }
        if (!isSupportedEncoding(parameters))
        {
            return std::nullopt;
        }

        const auto inputFormat = makeNativePcmInputFormat(parameters);
        if (inputFormat.mBytesPerFrame == 0 ||
            interleavedPcmBytes.size() % inputFormat.mBytesPerFrame != 0)
        {
            return std::nullopt;
        }

        const auto frameCount = static_cast<std::uint32_t>(
            interleavedPcmBytes.size() / inputFormat.mBytesPerFrame);
        const auto maxPacketBytes = maxOutputPacketBytes(parameters);
        if (maxPacketBytes >
            static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::nullopt;
        }

        ALACEncoder encoder;
        encoder.SetFrameSize(parameters.framesPerPacket);
        const auto outputFormat = makeAlacOutputFormat(parameters);
        if (encoder.InitializeEncoder(outputFormat) != ALAC_noErr)
        {
            return std::nullopt;
        }

        AlacEncodedPackets encoded{};
        encoded.frameCount = frameCount;
        encoded.framesPerPacket = parameters.framesPerPacket;
        const std::uint32_t packetCount =
            frameCount == 0
                ? 0
                : ((frameCount + parameters.framesPerPacket - 1u) /
                   parameters.framesPerPacket);
        encoded.packetSizes.reserve(packetCount);
        encoded.bytes.reserve(interleavedPcmBytes.size() +
                              static_cast<std::size_t>(packetCount) *
                                  kALACMaxEscapeHeaderBytes);

        const auto fullPacketInputBytes =
            static_cast<std::size_t>(parameters.framesPerPacket) *
            inputFormat.mBytesPerFrame;
        if (fullPacketInputBytes >
            static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::nullopt;
        }

        std::vector<unsigned char> packetInput(fullPacketInputBytes);
        std::vector<std::uint8_t> packetOutput(maxPacketBytes);
        for (std::uint32_t frameOffset = 0; frameOffset < frameCount;
             frameOffset += parameters.framesPerPacket)
        {
            const std::uint32_t packetFrames = std::min(
                parameters.framesPerPacket, frameCount - frameOffset);
            const auto packetInputBytes =
                static_cast<std::size_t>(packetFrames) *
                inputFormat.mBytesPerFrame;
            if (packetInputBytes >
                static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return std::nullopt;
            }

            std::fill(packetInput.begin(), packetInput.end(), 0);
            std::memcpy(packetInput.data(),
                        interleavedPcmBytes.data() +
                            static_cast<std::size_t>(frameOffset) *
                                inputFormat.mBytesPerFrame,
                        packetInputBytes);
            auto encodedByteCount = static_cast<std::int32_t>(packetInputBytes);
            auto *writeBuffer =
                reinterpret_cast<unsigned char *>(packetOutput.data());
            if (encoder.Encode(inputFormat, inputFormat, packetInput.data(),
                               writeBuffer, &encodedByteCount) !=
                ALAC_noErr)
            {
                return std::nullopt;
            }
            const auto encodedByteCount64 =
                static_cast<std::int64_t>(encodedByteCount);
            if (encodedByteCount64 <= 0 ||
                static_cast<std::uint64_t>(encodedByteCount64) >
                    packetOutput.size())
            {
                return std::nullopt;
            }

            encoded.packetSizes.push_back(
                static_cast<std::uint32_t>(encodedByteCount64));
            encoded.bytes.insert(
                encoded.bytes.end(), packetOutput.begin(),
                packetOutput.begin() +
                    static_cast<std::ptrdiff_t>(encodedByteCount64));
        }

        if (encoder.Finish() != ALAC_noErr)
        {
            return std::nullopt;
        }
        auto cookie = encoderCookieFrom(encoder, parameters.channels);
        if (!cookie.has_value())
        {
            return std::nullopt;
        }
        encoded.cookie = *cookie;
        return encoded;
    }

    std::optional<AlacDecodedPcm>
    decodePcmPackets(const AlacDecodingParameters &parameters,
                     const std::vector<std::uint8_t> &packetBytes,
                     const std::vector<std::uint32_t> &packetSizes)
    {
        if (parameters.sampleRate == 0 || parameters.channels == 0 ||
            parameters.channels > maxChannels() ||
            alacFormatFlagForBits(parameters.bitsPerSample) == 0 ||
            parameters.framesPerPacket != defaultFramesPerPacket() ||
            parameters.frameCount == 0 || parameters.magicCookie.empty() ||
            packetBytes.empty() || packetSizes.empty() ||
            parameters.packetFrameCounts.size() != packetSizes.size())
        {
            return std::nullopt;
        }

        const auto totalSampleCount =
            static_cast<std::size_t>(parameters.frameCount) *
            static_cast<std::size_t>(parameters.channels);
        if (parameters.channels != 0 &&
            totalSampleCount / parameters.channels != parameters.frameCount)
        {
            return std::nullopt;
        }

        ALACDecoder decoder;
        auto cookie = parameters.magicCookie;
        if (decoder.Init(cookie.data(), static_cast<std::uint32_t>(cookie.size())) !=
            ALAC_noErr ||
            !decoderConfigMatches(decoder, parameters))
        {
            return std::nullopt;
        }

        AlacDecodedPcm decoded{};
        decoded.sampleRate = parameters.sampleRate;
        decoded.channels = parameters.channels;
        decoded.bitsPerSample = parameters.bitsPerSample;
        decoded.frameCount = parameters.frameCount;
        const auto outputBytesPerSample = bytesPerSample(parameters.bitsPerSample);
        decoded.interleavedPcmBytes.reserve(totalSampleCount *
                                            outputBytesPerSample);

        const auto maxPacketSampleCount =
            static_cast<std::size_t>(decoder.mConfig.frameLength) *
            static_cast<std::size_t>(parameters.channels);
        std::vector<std::uint8_t> packetOutput(maxPacketSampleCount *
                                               outputBytesPerSample);

        std::size_t packetByteOffset = 0;
        std::uint32_t decodedFrames = 0;
        for (std::size_t packetIndex = 0; packetIndex < packetSizes.size();
             ++packetIndex)
        {
            const auto packetSize = packetSizes[packetIndex];
            const auto expectedPacketFrames =
                parameters.packetFrameCounts[packetIndex];
            if (packetSize == 0 || packetByteOffset > packetBytes.size() ||
                packetSize > packetBytes.size() - packetByteOffset ||
                expectedPacketFrames == 0 ||
                expectedPacketFrames > decoder.mConfig.frameLength ||
                decodedFrames > parameters.frameCount ||
                expectedPacketFrames > parameters.frameCount - decodedFrames)
            {
                return std::nullopt;
            }

            std::vector<std::uint8_t> packetInput(
                static_cast<std::size_t>(packetSize) + 3u, 0);
            std::copy(packetBytes.begin() +
                          static_cast<std::ptrdiff_t>(packetByteOffset),
                      packetBytes.begin() +
                          static_cast<std::ptrdiff_t>(packetByteOffset +
                                                      packetSize),
                      packetInput.begin());

            BitBuffer bitBuffer;
            BitBufferInit(&bitBuffer, packetInput.data(), packetSize);
            std::fill(packetOutput.begin(), packetOutput.end(), 0);

            std::uint32_t decodedPacketFrames = 0;
            if (decoder.Decode(&bitBuffer,
                               reinterpret_cast<std::uint8_t *>(
                                   packetOutput.data()),
                               decoder.mConfig.frameLength, parameters.channels,
                               &decodedPacketFrames) != ALAC_noErr ||
                decodedPacketFrames != expectedPacketFrames)
            {
                return std::nullopt;
            }

            const auto packetSampleCount =
                static_cast<std::size_t>(decodedPacketFrames) *
                static_cast<std::size_t>(parameters.channels);
            const auto packetByteCount = packetSampleCount * outputBytesPerSample;
            decoded.interleavedPcmBytes.insert(
                decoded.interleavedPcmBytes.end(), packetOutput.begin(),
                packetOutput.begin() +
                    static_cast<std::ptrdiff_t>(packetByteCount));

            decodedFrames += decodedPacketFrames;
            packetByteOffset += packetSize;
        }

        if (packetByteOffset != packetBytes.size() ||
            decodedFrames != parameters.frameCount ||
            decoded.interleavedPcmBytes.size() !=
                totalSampleCount * outputBytesPerSample)
        {
            return std::nullopt;
        }

        return decoded;
    }

    std::optional<AlacDecodedPcm16>
    decodePcm16Packets(const AlacDecodingParameters &parameters,
                       const std::vector<std::uint8_t> &packetBytes,
                       const std::vector<std::uint32_t> &packetSizes)
    {
        if (parameters.bitsPerSample != 16)
        {
            return std::nullopt;
        }

        const auto decodedBytes =
            decodePcmPackets(parameters, packetBytes, packetSizes);
        if (!decodedBytes.has_value() ||
            decodedBytes->interleavedPcmBytes.size() % sizeof(std::int16_t) != 0)
        {
            return std::nullopt;
        }

        AlacDecodedPcm16 decoded{};
        decoded.sampleRate = decodedBytes->sampleRate;
        decoded.channels = decodedBytes->channels;
        decoded.frameCount = decodedBytes->frameCount;
        decoded.interleavedSamples.resize(
            decodedBytes->interleavedPcmBytes.size() / sizeof(std::int16_t));
        std::memcpy(decoded.interleavedSamples.data(),
                    decodedBytes->interleavedPcmBytes.data(),
                    decodedBytes->interleavedPcmBytes.size());
        return decoded;
    }

} // namespace cupuacu::file::alac
