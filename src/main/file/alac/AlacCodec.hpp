#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace cupuacu::file::alac
{
    struct AlacEncodingParameters
    {
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        std::uint32_t bitsPerSample = 0;
        std::uint32_t framesPerPacket = 0;
    };

    struct AlacEncoderCookie
    {
        std::vector<std::uint8_t> bytes;
        std::uint32_t frameLength = 0;
        std::uint8_t bitDepth = 0;
        std::uint8_t channels = 0;
        std::uint32_t sampleRate = 0;
    };

    struct AlacEncodedPackets
    {
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint32_t> packetSizes;
        AlacEncoderCookie cookie;
        std::uint64_t frameCount = 0;
        std::uint32_t framesPerPacket = 0;
    };

    struct AlacEncodingSummary
    {
        AlacEncoderCookie cookie;
        std::uint64_t frameCount = 0;
        std::uint32_t framesPerPacket = 0;
    };

    // Worker-only; callbacks consume bounded packet views before returning.
    // False aborts encoding; exceptions propagate to the transactional writer.
    using PcmReadCallback =
        std::function<bool(std::uint64_t startFrame, std::uint32_t frames,
                           std::span<std::uint8_t> pcm)>;
    using EncodedPacketCallback =
        std::function<bool(std::span<const std::uint8_t> packet)>;
    [[nodiscard]] std::optional<AlacEncodingSummary>
    streamEncodedPcmPackets(AlacEncodingParameters parameters,
                            std::uint64_t frameCount, PcmReadCallback readPcm,
                            EncodedPacketCallback writePacket);

    struct AlacDecodingParameters
    {
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        std::uint32_t bitsPerSample = 0;
        std::uint32_t framesPerPacket = 0;
        std::uint64_t frameCount = 0;
        std::vector<std::uint8_t> magicCookie;
        std::vector<std::uint32_t> packetFrameCounts;
    };

    struct AlacDecodedPcm
    {
        std::vector<std::uint8_t> interleavedPcmBytes;
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        std::uint32_t bitsPerSample = 0;
        std::uint64_t frameCount = 0;
    };

    struct AlacDecodedPcm16
    {
        std::vector<std::int16_t> interleavedSamples;
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        std::uint64_t frameCount = 0;
    };

    using DecodeProgressCallback = std::function<void(
        std::uint64_t decodedFrames, std::uint64_t totalFrames)>;
    struct PacketBufferView
    {
        const std::uint8_t *bytes = nullptr;
        std::size_t accessibleByteCount = 0;
    };
    using PacketReadCallback =
        std::function<bool(std::uint64_t packetOffset, std::uint32_t packetSize,
                           PacketBufferView &packetView)>;
    using DecodedPacketCallback = std::function<bool(
        const std::uint8_t *interleavedPcmBytes, std::uint32_t pcmByteCount,
        std::uint32_t decodedFrames)>;

    [[nodiscard]] std::uint32_t defaultFramesPerPacket();
    [[nodiscard]] std::uint32_t maxChannels();
    [[nodiscard]] bool
    isSupportedEncoding(const AlacEncodingParameters &parameters);
    [[nodiscard]] std::optional<AlacEncoderCookie>
    makeEncoderCookie(AlacEncodingParameters parameters);
    [[nodiscard]] std::optional<AlacEncodedPackets>
    encodePcmPackets(AlacEncodingParameters parameters,
                     const std::vector<std::uint8_t> &interleavedPcmBytes);
    [[nodiscard]] std::optional<AlacDecodedPcm>
    decodePcmPackets(const AlacDecodingParameters &parameters,
                     const std::vector<std::uint8_t> &packetBytes,
                     const std::vector<std::uint32_t> &packetSizes,
                     DecodeProgressCallback progressCallback = {});
    [[nodiscard]] std::optional<AlacDecodedPcm>
    decodePcmPackets(const AlacDecodingParameters &parameters,
                     const std::vector<std::uint8_t> &sourceBytes,
                     const std::vector<std::uint64_t> &packetOffsets,
                     const std::vector<std::uint32_t> &packetSizes,
                     DecodeProgressCallback progressCallback = {});
    [[nodiscard]] std::optional<AlacDecodedPcm>
    decodePcmPackets(const AlacDecodingParameters &parameters,
                     const std::vector<std::uint64_t> &packetOffsets,
                     const std::vector<std::uint32_t> &packetSizes,
                     PacketReadCallback packetReadCallback,
                     DecodeProgressCallback progressCallback = {});
    [[nodiscard]] bool
    streamDecodedPcmPackets(const AlacDecodingParameters &parameters,
                            const std::vector<std::uint64_t> &packetOffsets,
                            const std::vector<std::uint32_t> &packetSizes,
                            PacketReadCallback packetReadCallback,
                            DecodedPacketCallback decodedPacketCallback,
                            DecodeProgressCallback progressCallback = {});
    [[nodiscard]] std::optional<AlacDecodedPcm16>
    decodePcm16Packets(const AlacDecodingParameters &parameters,
                       const std::vector<std::uint8_t> &packetBytes,
                       const std::vector<std::uint32_t> &packetSizes,
                       DecodeProgressCallback progressCallback = {});
    [[nodiscard]] std::optional<AlacDecodedPcm16>
    decodePcm16Packets(const AlacDecodingParameters &parameters,
                       const std::vector<std::uint8_t> &sourceBytes,
                       const std::vector<std::uint64_t> &packetOffsets,
                       const std::vector<std::uint32_t> &packetSizes,
                       DecodeProgressCallback progressCallback = {});
} // namespace cupuacu::file::alac
