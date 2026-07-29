#include "WebRtcAec3Backend.hpp"

#include <api/audio/echo_canceller3_config.h>
#include <modules/audio_processing/aec3/echo_canceller3.h>
#include <modules/audio_processing/audio_buffer.h>

#include <algorithm>
#include <optional>

namespace cupuacu::audio
{
    struct WebRtcAec3Backend::Impl
    {
        explicit Impl(const uint8_t channels)
            : captureChannels(channels),
              renderBuffer(kMonitorProcessingSampleRate, 2,
                           kMonitorProcessingSampleRate, 2,
                           kMonitorProcessingSampleRate, 2),
              captureBuffer(kMonitorProcessingSampleRate, channels,
                            kMonitorProcessingSampleRate, channels,
                            kMonitorProcessingSampleRate, channels)
        {
            const webrtc::EchoCanceller3Config monoConfig{};
            const auto multichannelConfig =
                webrtc::EchoCanceller3::CreateDefaultMultichannelConfig();
            canceller = std::make_unique<webrtc::EchoCanceller3>(
                monoConfig,
                std::optional<webrtc::EchoCanceller3Config>{multichannelConfig},
                kMonitorProcessingSampleRate, 2, channels);
            canceller->SetCaptureOutputUsage(true);
        }

        uint8_t captureChannels;
        webrtc::AudioBuffer renderBuffer;
        webrtc::AudioBuffer captureBuffer;
        std::unique_ptr<webrtc::EchoCanceller3> canceller;
    };

    WebRtcAec3Backend::WebRtcAec3Backend() = default;
    WebRtcAec3Backend::~WebRtcAec3Backend() = default;

    bool WebRtcAec3Backend::prepare(const uint8_t captureChannels)
    {
        if (captureChannels != 1 && captureChannels != 2)
        {
            impl.reset();
            return false;
        }

        try
        {
            impl = std::make_unique<Impl>(captureChannels);
        }
        catch (...)
        {
            impl.reset();
        }
        return impl != nullptr;
    }

    bool
    WebRtcAec3Backend::process(MonitorProcessingFrame &capture,
                               const MonitorProcessingFrame &renderReference,
                               const int delayMs, const bool echoPathChanged,
                               MonitorCancellationMetrics &metrics) noexcept
    {
        if (!impl || !impl->canceller)
        {
            return false;
        }

        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            std::transform(renderReference[channel].begin(),
                           renderReference[channel].end(),
                           impl->renderBuffer.channels()[channel],
                           [](const float sample)
                           {
                               return std::clamp(sample, -1.0f, 1.0f) *
                                      32768.0f;
                           });
        }
        impl->renderBuffer.SplitIntoFrequencyBands();
        impl->canceller->AnalyzeRender(&impl->renderBuffer);

        for (std::size_t channel = 0; channel < impl->captureChannels;
             ++channel)
        {
            std::transform(capture[channel].begin(), capture[channel].end(),
                           impl->captureBuffer.channels()[channel],
                           [](const float sample)
                           {
                               return std::clamp(sample, -1.0f, 1.0f) *
                                      32768.0f;
                           });
        }
        impl->captureBuffer.SplitIntoFrequencyBands();
        impl->canceller->AnalyzeCapture(&impl->captureBuffer);
        impl->canceller->SetAudioBufferDelay(std::clamp(delayMs, 0, 500));
        impl->canceller->ProcessCapture(&impl->captureBuffer, echoPathChanged);
        impl->captureBuffer.MergeFrequencyBands();

        for (std::size_t channel = 0; channel < impl->captureChannels;
             ++channel)
        {
            std::transform(impl->captureBuffer.channels()[channel],
                           impl->captureBuffer.channels()[channel] +
                               kMonitorProcessingFrameCount,
                           capture[channel].begin(),
                           [](const float sample)
                           {
                               return std::clamp(sample, -32768.0f, 32768.0f) /
                                      32768.0f;
                           });
        }

        const auto aecMetrics = impl->canceller->GetMetrics();
        metrics.echoReturnLossDb =
            static_cast<float>(aecMetrics.echo_return_loss);
        metrics.echoReturnLossEnhancementDb =
            static_cast<float>(aecMetrics.echo_return_loss_enhancement);
        metrics.delayMs = aecMetrics.delay_ms;
        metrics.active = impl->canceller->ActiveProcessing();
        return true;
    }
} // namespace cupuacu::audio
