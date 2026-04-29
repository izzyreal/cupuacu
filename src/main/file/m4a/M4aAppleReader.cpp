#include "M4aAppleReader.hpp"

#if defined(__APPLE__)

#include "M4aParser.hpp"

#include <AudioToolbox/ExtendedAudioFile.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace cupuacu::file::m4a
{
    namespace
    {
        [[nodiscard]] cupuacu::SampleFormat sampleFormatForBitDepth(
            const std::uint16_t bitDepth)
        {
            switch (bitDepth)
            {
                case 16:
                    return cupuacu::SampleFormat::PCM_S16;
                case 24:
                    return cupuacu::SampleFormat::PCM_S24;
                case 32:
                    return cupuacu::SampleFormat::PCM_S32;
                default:
                    throw std::runtime_error(
                        "Unsupported ALAC bit depth in M4A file");
            }
        }

        [[nodiscard]] std::string describeOsStatus(const OSStatus status)
        {
            return std::to_string(static_cast<std::int32_t>(status));
        }

        class ScopedCfUrl
        {
        public:
            explicit ScopedCfUrl(const std::filesystem::path &path)
            {
                const auto nativePath = path.string();
                url = CFURLCreateFromFileSystemRepresentation(
                    kCFAllocatorDefault,
                    reinterpret_cast<const UInt8 *>(nativePath.c_str()),
                    static_cast<CFIndex>(nativePath.size()), false);
                if (!url)
                {
                    throw std::runtime_error("Failed to create file URL for " +
                                             path.string());
                }
            }

            ~ScopedCfUrl()
            {
                if (url)
                {
                    CFRelease(url);
                }
            }

            ScopedCfUrl(const ScopedCfUrl &) = delete;
            ScopedCfUrl &operator=(const ScopedCfUrl &) = delete;

            [[nodiscard]] CFURLRef get() const
            {
                return url;
            }

        private:
            CFURLRef url = nullptr;
        };

        class ScopedExtAudioFile
        {
        public:
            ScopedExtAudioFile() = default;

            ~ScopedExtAudioFile()
            {
                if (file)
                {
                    ExtAudioFileDispose(file);
                }
            }

            ScopedExtAudioFile(const ScopedExtAudioFile &) = delete;
            ScopedExtAudioFile &operator=(const ScopedExtAudioFile &) = delete;

            [[nodiscard]] ExtAudioFileRef *out()
            {
                return &file;
            }

            [[nodiscard]] ExtAudioFileRef get() const
            {
                return file;
            }

        private:
            ExtAudioFileRef file = nullptr;
        };
    } // namespace

    M4aAlacFileInfo streamAlacM4aFileWithAudioToolbox(
        const std::filesystem::path &path,
        M4aDecodedFloatBlockCallback decodedFloatBlockCallback,
        M4aAlacFileInfoCallback fileInfoCallback,
        M4aFloatProgressCallback progressCallback)
    {
        if (!decodedFloatBlockCallback)
        {
            throw std::runtime_error("Missing M4A ALAC decode sink");
        }

        const auto parsed = parseAlacM4aFile(path);
        if (parsed.bitDepth != 16 && parsed.bitDepth != 24 &&
            parsed.bitDepth != 32)
        {
            throw std::runtime_error(
                "Only 16, 24, and 32-bit ALAC M4A files are currently supported");
        }

        const auto totalFrames = parsed.frameCount;
        const M4aAlacFileInfo fileInfo{
            .sampleRate = parsed.sampleRate,
            .channels = parsed.channels,
            .bitDepth = parsed.bitDepth,
            .frameCount = totalFrames,
            .sampleFormat = sampleFormatForBitDepth(parsed.bitDepth),
            .markers = parsed.markers,
        };
        if (fileInfoCallback)
        {
            fileInfoCallback(fileInfo);
        }

        ScopedCfUrl url(path);
        ScopedExtAudioFile audioFile;
        auto status = ExtAudioFileOpenURL(url.get(), audioFile.out());
        if (status != noErr || !audioFile.get())
        {
            throw std::runtime_error("Failed to open M4A file with AudioToolbox: " +
                                     path.string() + " (" +
                                     describeOsStatus(status) + ")");
        }

        AudioStreamBasicDescription sourceFormat{};
        UInt32 sourceFormatSize = sizeof(sourceFormat);
        status = ExtAudioFileGetProperty(audioFile.get(),
                                         kExtAudioFileProperty_FileDataFormat,
                                         &sourceFormatSize, &sourceFormat);
        if (status != noErr)
        {
            throw std::runtime_error(
                "Failed to read M4A source format from AudioToolbox: " +
                path.string() + " (" + describeOsStatus(status) + ")");
        }

        AudioStreamBasicDescription clientFormat{};
        clientFormat.mSampleRate = sourceFormat.mSampleRate;
        clientFormat.mFormatID = kAudioFormatLinearPCM;
        clientFormat.mFormatFlags =
            kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        clientFormat.mBytesPerPacket = sizeof(float);
        clientFormat.mFramesPerPacket = 1;
        clientFormat.mBytesPerFrame = sizeof(float);
        clientFormat.mChannelsPerFrame = parsed.channels;
        clientFormat.mBitsPerChannel = 8u * sizeof(float);

        status = ExtAudioFileSetProperty(audioFile.get(),
                                         kExtAudioFileProperty_ClientDataFormat,
                                         sizeof(clientFormat), &clientFormat);
        if (status != noErr)
        {
            throw std::runtime_error(
                "Failed to set M4A client format for AudioToolbox: " +
                path.string() + " (" + describeOsStatus(status) + ")");
        }

        constexpr std::uint32_t kLoadBlockFrames = 65536u;
        std::vector<std::vector<float>> planar(
            parsed.channels,
            std::vector<float>(static_cast<std::size_t>(kLoadBlockFrames)));
        std::vector<AudioBuffer> buffers(parsed.channels);
        std::vector<float> interleaved(
            static_cast<std::size_t>(kLoadBlockFrames) * parsed.channels);
        std::vector<std::byte> audioBufferListStorage(
            sizeof(AudioBufferList) +
            sizeof(AudioBuffer) * (parsed.channels > 0 ? parsed.channels - 1u
                                                       : 0u));
        auto *audioBufferList =
            reinterpret_cast<AudioBufferList *>(audioBufferListStorage.data());
        audioBufferList->mNumberBuffers = parsed.channels;
        for (std::uint16_t channel = 0; channel < parsed.channels; ++channel)
        {
            buffers[channel].mNumberChannels = 1;
            buffers[channel].mData = planar[channel].data();
            buffers[channel].mDataByteSize =
                static_cast<UInt32>(planar[channel].size() * sizeof(float));
            audioBufferList->mBuffers[channel] = buffers[channel];
        }

        std::uint32_t framesReadTotal = 0;
        if (progressCallback)
        {
            progressCallback(0, totalFrames);
        }
        while (framesReadTotal < totalFrames)
        {
            UInt32 framesToRead =
                std::min<std::uint32_t>(kLoadBlockFrames,
                                        totalFrames - framesReadTotal);
            for (std::uint16_t channel = 0; channel < parsed.channels; ++channel)
            {
                audioBufferList->mBuffers[channel].mDataByteSize =
                    framesToRead * static_cast<UInt32>(sizeof(float));
            }

            status = ExtAudioFileRead(audioFile.get(), &framesToRead,
                                      audioBufferList);
            if (status != noErr)
            {
                throw std::runtime_error(
                    "Failed to decode M4A file with AudioToolbox: " +
                    path.string() + " (" + describeOsStatus(status) + ")");
            }
            if (framesToRead == 0)
            {
                break;
            }

            for (UInt32 frame = 0; frame < framesToRead; ++frame)
            {
                for (std::uint16_t channel = 0; channel < parsed.channels;
                     ++channel)
                {
                    interleaved[static_cast<std::size_t>(frame) *
                                    static_cast<std::size_t>(parsed.channels) +
                                channel] = planar[channel][frame];
                }
            }
            decodedFloatBlockCallback(interleaved.data(), framesToRead,
                                      parsed.channels);
            framesReadTotal += framesToRead;
            if (progressCallback)
            {
                progressCallback(framesReadTotal, totalFrames);
            }
        }

        if (framesReadTotal != totalFrames)
        {
            throw std::runtime_error(
                "Decoded M4A sample count mismatch in AudioToolbox path");
        }

        return fileInfo;
    }
} // namespace cupuacu::file::m4a

#endif
