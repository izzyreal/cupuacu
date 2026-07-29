#include "WebRtcAec3Backend.hpp"

#include <api/audio/echo_canceller3_config.h>
#include <modules/audio_processing/aec3/echo_canceller3.h>
#include <modules/audio_processing/audio_buffer.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace cupuacu::audio
{
    namespace
    {
        constexpr float kComfortNoiseFloorDbfs = -120.0f;
        constexpr float kResidualTailThreshold = 0.006f;
        constexpr float kResidualTailRenderThreshold = 0.0005f;
        constexpr float kResidualTailExitThreshold = 0.02f;
        constexpr float kResidualTailInputRatio = 1.35f;
        constexpr float kResidualTailMaximumErleDb = 3.0f;
        constexpr float kResidualTailMinimumGain = 0.25f;
        constexpr int kResidualTailHoldFrames = 100;
        constexpr int kResidualTailArmedFrames = 500;
    } // namespace

    struct WebRtcAec3Backend::Impl
    {
        Impl(const uint8_t channels, const FeedbackSuppressionMode mode)
            : captureChannels(channels), mode(mode),
              renderBuffer(kMonitorProcessingSampleRate, 2,
                           kMonitorProcessingSampleRate, 2,
                           kMonitorProcessingSampleRate, 2),
              captureBuffer(kMonitorProcessingSampleRate, channels,
                            kMonitorProcessingSampleRate, channels,
                            kMonitorProcessingSampleRate, channels)
        {
            webrtc::EchoCanceller3Config monoConfig{};
            auto multichannelConfig =
                webrtc::EchoCanceller3::CreateDefaultMultichannelConfig();
            if (mode == FeedbackSuppressionMode::Smooth)
            {
                // AEC3 adds shaped comfort noise when nonlinear suppression is
                // active. Its telephony-oriented default floor can become
                // audible in a high-gain live monitor, so the Smooth profile
                // keeps only a much lower numerical floor.
                monoConfig.comfort_noise.noise_floor_dbfs =
                    kComfortNoiseFloorDbfs;
                multichannelConfig.comfort_noise.noise_floor_dbfs =
                    kComfortNoiseFloorDbfs;
            }
            canceller = std::make_unique<webrtc::EchoCanceller3>(
                monoConfig,
                std::optional<webrtc::EchoCanceller3Config>{multichannelConfig},
                kMonitorProcessingSampleRate, 2, channels);
            canceller->SetCaptureOutputUsage(true);
        }

        uint8_t captureChannels;
        FeedbackSuppressionMode mode;
        webrtc::AudioBuffer renderBuffer;
        webrtc::AudioBuffer captureBuffer;
        std::unique_ptr<webrtc::EchoCanceller3> canceller;
        float residualTailGain = 1.0f;
        int residualTailHoldFrames = 0;
        int residualTailArmedFrames = 0;
    };

    WebRtcAec3Backend::WebRtcAec3Backend(
        const FeedbackSuppressionMode modeToUse)
        : mode(modeToUse)
    {
    }
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
            impl = std::make_unique<Impl>(captureChannels, mode);
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
                               const bool feedbackRisk,
                               MonitorCancellationMetrics &metrics) noexcept
    {
        if (!impl || !impl->canceller)
        {
            return false;
        }

        double captureInputPower = 0.0;
        double renderInputPower = 0.0;
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
            for (const float sample : renderReference[channel])
            {
                renderInputPower += static_cast<double>(sample) * sample;
            }
        }
        impl->renderBuffer.SplitIntoFrequencyBands();
        impl->canceller->AnalyzeRender(&impl->renderBuffer);

        for (std::size_t channel = 0; channel < impl->captureChannels;
             ++channel)
        {
            for (const float sample : capture[channel])
            {
                captureInputPower += static_cast<double>(sample) * sample;
            }
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

        double captureOutputPower = 0.0;
        for (std::size_t channel = 0; channel < impl->captureChannels;
             ++channel)
        {
            for (std::size_t sample = 0; sample < kMonitorProcessingFrameCount;
                 ++sample)
            {
                const double normalized =
                    static_cast<double>(
                        impl->captureBuffer.channels()[channel][sample]) /
                    32768.0;
                captureOutputPower += normalized * normalized;
            }
        }

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

        const auto captureSampleCount = static_cast<double>(
            kMonitorProcessingFrameCount * impl->captureChannels);
        const auto renderSampleCount =
            static_cast<double>(kMonitorProcessingFrameCount * 2);
        const float captureInputRms = static_cast<float>(
            std::sqrt(captureInputPower / captureSampleCount));
        const float captureOutputRms = static_cast<float>(
            std::sqrt(captureOutputPower / captureSampleCount));
        const float renderInputRms =
            static_cast<float>(std::sqrt(renderInputPower / renderSampleCount));

        // AEC3's nonlinear suppressor replaces residual echo with shaped
        // comfort noise. In a high-gain acoustic loop that low-level output can
        // become its own render input and form a persistent "wind" tail. Only
        // attenuate when the signal is quiet, echo-dominated, and AEC3 reports
        // little current enhancement; ordinary quiet input with no speaker
        // return remains untouched.
        if (impl->mode != FeedbackSuppressionMode::Smooth)
        {
            return true;
        }

        if (feedbackRisk)
        {
            impl->residualTailArmedFrames = kResidualTailArmedFrames;
        }
        else if (impl->residualTailArmedFrames > 0)
        {
            --impl->residualTailArmedFrames;
        }
        const bool residualTailDetected =
            impl->residualTailArmedFrames > 0 &&
            renderInputRms > kResidualTailRenderThreshold &&
            captureOutputRms < kResidualTailThreshold &&
            captureInputRms > captureOutputRms * kResidualTailInputRatio &&
            metrics.echoReturnLossEnhancementDb < kResidualTailMaximumErleDb;
        if (captureOutputRms > kResidualTailExitThreshold)
        {
            impl->residualTailHoldFrames = 0;
        }
        else if (residualTailDetected)
        {
            impl->residualTailHoldFrames = kResidualTailHoldFrames;
        }
        else if (impl->residualTailHoldFrames > 0)
        {
            --impl->residualTailHoldFrames;
        }

        const float targetTailGain =
            impl->residualTailHoldFrames > 0 ? kResidualTailMinimumGain : 1.0f;
        const float smoothing =
            targetTailGain < impl->residualTailGain
                ? 0.03f
                : (captureOutputRms > kResidualTailExitThreshold ? 0.3f
                                                                 : 0.005f);
        impl->residualTailGain +=
            smoothing * (targetTailGain - impl->residualTailGain);

        for (std::size_t channel = 0; channel < impl->captureChannels;
             ++channel)
        {
            for (float &sample : capture[channel])
            {
                sample *= impl->residualTailGain;
            }
        }
        return true;
    }
} // namespace cupuacu::audio
