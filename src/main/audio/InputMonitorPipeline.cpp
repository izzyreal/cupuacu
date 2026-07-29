#include "InputMonitorPipeline.hpp"

#include "WebRtcAec3Backend.hpp"

#include <common_audio/resampler/push_sinc_resampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace cupuacu::audio
{
    namespace
    {
        constexpr std::size_t kDeviceFrameCount = 441;
        constexpr std::size_t kOutputFifoFrames = 4096;
        constexpr std::size_t kOutputPrimeFrames = kDeviceFrameCount * 2;
        constexpr std::size_t kFftSize = 2048;
        constexpr float kMinimumPower = 1.0e-12f;
        constexpr int kDecorrelationReleaseFrames = 20;

        class FrequencyShifter
        {
        public:
            FrequencyShifter()
            {
                constexpr int center = (kTapCount - 1) / 2;
                for (int tap = 0; tap < kTapCount; ++tap)
                {
                    const int offset = tap - center;
                    if (offset != 0 && (std::abs(offset) % 2) == 1)
                    {
                        const float ideal = 2.0f / (std::numbers::pi_v<float> *
                                                    static_cast<float>(offset));
                        const float phase = 2.0f * std::numbers::pi_v<float> *
                                            static_cast<float>(tap) /
                                            static_cast<float>(kTapCount - 1);
                        const float window = 0.42f - 0.5f * std::cos(phase) +
                                             0.08f * std::cos(2.0f * phase);
                        coefficients[static_cast<std::size_t>(tap)] =
                            ideal * window;
                    }
                }
            }

            void setTargetShift(const float hz) noexcept
            {
                targetShiftHz = std::clamp(hz, 0.0f, 8.0f);
                targetMix = targetShiftHz > 0.0f ? 1.0f : 0.0f;
            }

            void reset() noexcept
            {
                histories = {};
                writeIndex = 0;
                oscillatorPhase = 0.0f;
                currentShiftHz = 0.0f;
                targetShiftHz = 0.0f;
                currentMix = 0.0f;
                targetMix = 0.0f;
            }

            void process(MonitorProcessingFrame &frame, const uint8_t channels,
                         const bool smoothTransitions) noexcept
            {
                const float shiftStep =
                    (targetShiftHz - currentShiftHz) /
                    static_cast<float>(kMonitorProcessingFrameCount);
                const float mixStep =
                    ((smoothTransitions ? targetMix : 1.0f) - currentMix) /
                    static_cast<float>(kMonitorProcessingFrameCount);
                for (std::size_t sample = 0;
                     sample < kMonitorProcessingFrameCount; ++sample)
                {
                    currentShiftHz += shiftStep;
                    currentMix = std::clamp(currentMix + mixStep, 0.0f, 1.0f);
                    oscillatorPhase +=
                        2.0f * std::numbers::pi_v<float> * currentShiftHz /
                        static_cast<float>(kMonitorProcessingSampleRate);
                    if (oscillatorPhase > 2.0f * std::numbers::pi_v<float>)
                    {
                        oscillatorPhase -= 2.0f * std::numbers::pi_v<float>;
                    }
                    const float cosine = std::cos(oscillatorPhase);
                    const float sine = std::sin(oscillatorPhase);

                    for (std::size_t channel = 0; channel < channels; ++channel)
                    {
                        histories[channel][writeIndex] = frame[channel][sample];

                        float quadrature = 0.0f;
                        for (std::size_t tap = 0; tap < kTapCount; ++tap)
                        {
                            const std::size_t historyIndex =
                                (writeIndex + kTapCount - tap) % kTapCount;
                            quadrature += coefficients[tap] *
                                          histories[channel][historyIndex];
                        }
                        const std::size_t delayedIndex =
                            (writeIndex + kTapCount -
                             static_cast<std::size_t>((kTapCount - 1) / 2)) %
                            kTapCount;
                        const float inPhase = histories[channel][delayedIndex];
                        const float shifted =
                            inPhase * cosine - quadrature * sine;
                        frame[channel][sample] =
                            smoothTransitions
                                ? inPhase + currentMix * (shifted - inPhase)
                                : shifted;
                    }
                    writeIndex = (writeIndex + 1) % kTapCount;
                }

                if (smoothTransitions && targetMix == 0.0f &&
                    currentMix < 1.0e-5f)
                {
                    currentMix = 0.0f;
                    currentShiftHz = 0.0f;
                    oscillatorPhase = 0.0f;
                }
            }

        private:
            static constexpr std::size_t kTapCount = 63;
            std::array<float, kTapCount> coefficients{};
            std::array<std::array<float, kTapCount>, kMonitorMaxChannels>
                histories{};
            std::size_t writeIndex = 0;
            float oscillatorPhase = 0.0f;
            float currentShiftHz = 0.0f;
            float targetShiftHz = 0.0f;
            float currentMix = 0.0f;
            float targetMix = 0.0f;
        };

        struct FeedbackDecision
        {
            float shiftHz = 0.0f;
            bool trip = false;
        };

        class FeedbackDetector
        {
        public:
            FeedbackDetector()
            {
                for (std::size_t sample = 0; sample < kFftSize; ++sample)
                {
                    window[sample] =
                        0.5f -
                        0.5f * std::cos(2.0f * std::numbers::pi_v<float> *
                                        static_cast<float>(sample) /
                                        static_cast<float>(kFftSize - 1));
                }
                for (std::size_t index = 0; index < kFftSize / 2; ++index)
                {
                    const float angle = -2.0f * std::numbers::pi_v<float> *
                                        static_cast<float>(index) /
                                        static_cast<float>(kFftSize);
                    twiddles[index] = {std::cos(angle), std::sin(angle)};
                }
            }

            FeedbackDecision
            analyze(const std::array<std::array<float, kDeviceFrameCount>,
                                     kMonitorMaxChannels> &capture,
                    const std::array<std::array<float, kDeviceFrameCount>,
                                     kMonitorMaxChannels> &render,
                    const uint8_t channels,
                    const bool smoothTransitions) noexcept
            {
                bool clipped = false;
                for (std::size_t sample = 0; sample < kDeviceFrameCount;
                     ++sample)
                {
                    const float captureMono =
                        channels == 2
                            ? 0.5f * (capture[0][sample] + capture[1][sample])
                            : capture[0][sample];
                    const float renderMono =
                        0.5f * (render[0][sample] + render[1][sample]);
                    captureHistory[historyWrite] = captureMono;
                    renderHistory[historyWrite] = renderMono;
                    historyWrite = (historyWrite + 1) % kFftSize;
                    historyCount = std::min(historyCount + 1, kFftSize);
                    clipped |= std::abs(renderMono) >= 0.98f;
                }

                clippingFrames = clipped ? clippingFrames + 1
                                         : std::max(0, clippingFrames - 1);
                if (historyCount < kFftSize)
                {
                    return {.shiftHz = 0.0f, .trip = clippingFrames >= 3};
                }

                prepareSpectrum(captureHistory, captureSpectrum);
                prepareSpectrum(renderHistory, renderSpectrum);
                fft(captureSpectrum);
                fft(renderSpectrum);

                float totalRenderPower = 0.0f;
                float maximumRenderPower = 0.0f;
                std::size_t maximumBin = 0;
                constexpr std::size_t firstBin = 4;
                constexpr std::size_t lastBin = kFftSize / 2;
                for (std::size_t bin = firstBin; bin < lastBin; ++bin)
                {
                    const float capturePower = std::norm(captureSpectrum[bin]);
                    const float renderPower = std::norm(renderSpectrum[bin]);
                    capturePowerAverage[bin] =
                        0.75f * capturePowerAverage[bin] + 0.25f * capturePower;
                    renderPowerAverage[bin] =
                        0.75f * renderPowerAverage[bin] + 0.25f * renderPower;
                    crossPowerAverage[bin] = 0.75f * crossPowerAverage[bin] +
                                             0.25f * captureSpectrum[bin] *
                                                 std::conj(renderSpectrum[bin]);
                    totalRenderPower += renderPower;
                    if (renderPower > maximumRenderPower)
                    {
                        maximumRenderPower = renderPower;
                        maximumBin = bin;
                    }
                }

                const float averageRenderPower =
                    totalRenderPower / static_cast<float>(lastBin - firstBin);
                const float crestDb =
                    10.0f * std::log10((maximumRenderPower + kMinimumPower) /
                                       (averageRenderPower + kMinimumPower));
                const float maximumDb =
                    10.0f * std::log10(maximumRenderPower + kMinimumPower);
                const float growthDb = maximumDb - previousMaximumDb;
                previousMaximumDb = maximumDb;

                const float crossMagnitudeSquared =
                    std::norm(crossPowerAverage[maximumBin]);
                const float coherence = crossMagnitudeSquared /
                                        (capturePowerAverage[maximumBin] *
                                             renderPowerAverage[maximumBin] +
                                         kMinimumPower);

                const bool risk = maximumDb > -35.0f && crestDb > 14.0f &&
                                  growthDb > 1.5f && coherence > 0.65f;
                riskFrames =
                    risk ? riskFrames + 1 : std::max(0, riskFrames - 1);

                float requestedShiftHz = 0.0f;
                if (riskFrames >= 6)
                {
                    requestedShiftHz = 8.0f;
                }
                else if (riskFrames >= 2)
                {
                    requestedShiftHz = 5.0f;
                }

                if (!smoothTransitions)
                {
                    heldShiftHz = requestedShiftHz;
                    releaseFrames = 0;
                }
                else if (requestedShiftHz > 0.0f)
                {
                    heldShiftHz = std::max(heldShiftHz, requestedShiftHz);
                    releaseFrames = kDecorrelationReleaseFrames;
                }
                else if (releaseFrames > 0)
                {
                    --releaseFrames;
                }
                else
                {
                    heldShiftHz = 0.0f;
                }

                return {.shiftHz = heldShiftHz,
                        .trip = riskFrames >= 12 || clippingFrames >= 3};
            }

            void reset() noexcept
            {
                captureHistory = {};
                renderHistory = {};
                captureSpectrum = {};
                renderSpectrum = {};
                capturePowerAverage = {};
                renderPowerAverage = {};
                crossPowerAverage = {};
                historyWrite = 0;
                historyCount = 0;
                previousMaximumDb = -120.0f;
                riskFrames = 0;
                clippingFrames = 0;
                releaseFrames = 0;
                heldShiftHz = 0.0f;
            }

        private:
            using Spectrum = std::array<std::complex<float>, kFftSize>;

            void prepareSpectrum(const std::array<float, kFftSize> &history,
                                 Spectrum &spectrum) const noexcept
            {
                for (std::size_t sample = 0; sample < kFftSize; ++sample)
                {
                    const std::size_t historyIndex =
                        (historyWrite + sample) % kFftSize;
                    spectrum[sample] = {history[historyIndex] * window[sample],
                                        0.0f};
                }
            }

            void fft(Spectrum &values) const noexcept
            {
                for (std::size_t i = 1, j = 0; i < kFftSize; ++i)
                {
                    std::size_t bit = kFftSize >> 1;
                    for (; j & bit; bit >>= 1)
                    {
                        j ^= bit;
                    }
                    j ^= bit;
                    if (i < j)
                    {
                        std::swap(values[i], values[j]);
                    }
                }

                for (std::size_t length = 2; length <= kFftSize; length <<= 1)
                {
                    for (std::size_t base = 0; base < kFftSize; base += length)
                    {
                        for (std::size_t offset = 0; offset < length / 2;
                             ++offset)
                        {
                            const auto twiddle =
                                twiddles[offset * (kFftSize / length)];
                            const auto even = values[base + offset];
                            const auto odd =
                                values[base + offset + length / 2] * twiddle;
                            values[base + offset] = even + odd;
                            values[base + offset + length / 2] = even - odd;
                        }
                    }
                }
            }

            std::array<float, kFftSize> captureHistory{};
            std::array<float, kFftSize> renderHistory{};
            Spectrum captureSpectrum{};
            Spectrum renderSpectrum{};
            std::array<float, kFftSize> window{};
            std::array<std::complex<float>, kFftSize / 2> twiddles{};
            std::array<float, kFftSize / 2> capturePowerAverage{};
            std::array<float, kFftSize / 2> renderPowerAverage{};
            std::array<std::complex<float>, kFftSize / 2> crossPowerAverage{};
            std::size_t historyWrite = 0;
            std::size_t historyCount = 0;
            float previousMaximumDb = -120.0f;
            int riskFrames = 0;
            int clippingFrames = 0;
            int releaseFrames = 0;
            float heldShiftHz = 0.0f;
        };
    } // namespace

    struct InputMonitorPipeline::Impl
    {
        explicit Impl(const MonitorStreamFormat &streamFormat)
            : format(streamFormat),
              captureUpsamplers{
                  std::make_unique<webrtc::PushSincResampler>(
                      kDeviceFrameCount, kMonitorProcessingFrameCount),
                  std::make_unique<webrtc::PushSincResampler>(
                      kDeviceFrameCount, kMonitorProcessingFrameCount)},
              outputDownsamplers{
                  std::make_unique<webrtc::PushSincResampler>(
                      kMonitorProcessingFrameCount, kDeviceFrameCount),
                  std::make_unique<webrtc::PushSincResampler>(
                      kMonitorProcessingFrameCount, kDeviceFrameCount)},
              renderUpsamplers{
                  std::make_unique<webrtc::PushSincResampler>(
                      kDeviceFrameCount, kMonitorProcessingFrameCount),
                  std::make_unique<webrtc::PushSincResampler>(
                      kDeviceFrameCount, kMonitorProcessingFrameCount)}
        {
        }

        void clearSessionBuffers() noexcept
        {
            inputFill = 0;
            outputRead = 0;
            outputWrite = 0;
            outputCount = 0;
            outputPrimed = false;
            warmupFrames = 0;
            gain = mode == FeedbackSuppressionMode::Smooth ? 0.0f : 1.0f;
            tripRampRemaining = 0;
            tripRampGainStep = 0.0f;
            pendingTerminalState = MonitorProtectionState::Inactive;
            smoothedDelayMs = 0.0f;
            delayInitialized = false;
            echoPathChanged = true;
            feedbackRisk = false;
            renderReference = {};
            frequencyShifter.reset();
            detector.reset();
        }

        void
        beginEmergencyStop(const MonitorProtectionState terminalState) noexcept
        {
            if (pendingTerminalState != MonitorProtectionState::Inactive)
            {
                return;
            }
            pendingTerminalState = terminalState;
            tripRampRemaining =
                std::max<std::size_t>(1, format.sampleRate / 200);
            tripRampGainStep = gain / static_cast<float>(tripRampRemaining);
            telemetry.state = MonitorProtectionState::Decorrelating;
        }

        void completeEmergencyStop() noexcept
        {
            telemetry.state = pendingTerminalState;
            pendingTerminalState = MonitorProtectionState::Inactive;
            sessionActive = false;
            ++telemetry.tripGeneration;
        }

        int estimateDelayMs(const AudioCallbackTiming &timing) noexcept
        {
            double seconds =
                format.inputLatencySeconds + format.outputLatencySeconds;
            if (timing.valid && std::isfinite(timing.inputAdcTime) &&
                std::isfinite(timing.outputDacTime) &&
                timing.outputDacTime >= timing.inputAdcTime)
            {
                seconds = timing.outputDacTime - timing.inputAdcTime;
            }
            seconds += static_cast<double>(outputCount) /
                       static_cast<double>(format.sampleRate);
            const float measured =
                static_cast<float>(std::clamp(seconds * 1000.0, 0.0, 500.0));
            smoothedDelayMs = delayInitialized
                                  ? 0.9f * smoothedDelayMs + 0.1f * measured
                                  : measured;
            delayInitialized = true;
            return static_cast<int>(std::lround(smoothedDelayMs));
        }

        bool processDeviceFrame(const AudioCallbackTiming &timing) noexcept
        {
            for (std::size_t channel = 0; channel < format.inputChannels;
                 ++channel)
            {
                captureUpsamplers[channel]->Resample(
                    inputFrame[channel].data(), kDeviceFrameCount,
                    processingFrame[channel].data(),
                    kMonitorProcessingFrameCount);
            }

            MonitorCancellationMetrics metrics{};
            FeedbackDecision decision{};
            if (mode != FeedbackSuppressionMode::Off)
            {
                auto *backend = activeBackend();
                if (!backend ||
                    !backend->process(processingFrame, renderReference,
                                      estimateDelayMs(timing), echoPathChanged,
                                      feedbackRisk, metrics))
                {
                    beginEmergencyStop(MonitorProtectionState::Unavailable);
                    return false;
                }
                echoPathChanged = false;
            }

            if (format.inputChannels == 1)
            {
                processingFrame[1] = processingFrame[0];
            }
            if (mode != FeedbackSuppressionMode::Off)
            {
                frequencyShifter.process(processingFrame, 2,
                                         mode ==
                                             FeedbackSuppressionMode::Smooth);
            }

            for (std::size_t channel = 0; channel < 2; ++channel)
            {
                outputDownsamplers[channel]->Resample(
                    processingFrame[channel].data(),
                    kMonitorProcessingFrameCount,
                    processedDeviceFrame[channel].data(), kDeviceFrameCount);
            }

            if (mode != FeedbackSuppressionMode::Off)
            {
                decision = detector.analyze(
                    inputFrame, processedDeviceFrame, format.inputChannels,
                    mode == FeedbackSuppressionMode::Smooth);
                feedbackRisk = decision.shiftHz > 0.0f;
                frequencyShifter.setTargetShift(decision.shiftHz);
                if (decision.trip)
                {
                    beginEmergencyStop(MonitorProtectionState::Tripped);
                }
            }

            if (mode != FeedbackSuppressionMode::Off)
            {
                for (std::size_t channel = 0; channel < 2; ++channel)
                {
                    renderUpsamplers[channel]->Resample(
                        processedDeviceFrame[channel].data(), kDeviceFrameCount,
                        renderReference[channel].data(),
                        kMonitorProcessingFrameCount);
                }
            }

            for (std::size_t sample = 0; sample < kDeviceFrameCount; ++sample)
            {
                if (outputCount >= kOutputFifoFrames)
                {
                    break;
                }
                outputFifo[0][outputWrite] = processedDeviceFrame[0][sample];
                outputFifo[1][outputWrite] = processedDeviceFrame[1][sample];
                outputWrite = (outputWrite + 1) % kOutputFifoFrames;
                ++outputCount;
            }

            ++warmupFrames;
            if (pendingTerminalState == MonitorProtectionState::Inactive)
            {
                telemetry.state =
                    mode == FeedbackSuppressionMode::Off
                        ? MonitorProtectionState::Disabled
                    : decision.shiftHz > 0.0f
                        ? MonitorProtectionState::Decorrelating
                        : (warmupFrames < 2 ? MonitorProtectionState::WarmingUp
                                            : MonitorProtectionState::Active);
            }
            telemetry.estimatedDelayMs =
                metrics.delayMs > 0
                    ? metrics.delayMs
                    : static_cast<int>(std::lround(smoothedDelayMs));
            telemetry.echoReturnLossDb = metrics.echoReturnLossDb;
            telemetry.echoReturnLossEnhancementDb =
                metrics.echoReturnLossEnhancementDb;
            return true;
        }

        MonitorCancellationBackend *activeBackend() const noexcept
        {
            if (customBackend)
            {
                return customBackend.get();
            }
            return mode == FeedbackSuppressionMode::Standard
                       ? standardBackend.get()
                       : smoothBackend.get();
        }

        bool
        isModeAvailable(const FeedbackSuppressionMode candidate) const noexcept
        {
            if (candidate == FeedbackSuppressionMode::Off)
            {
                return true;
            }
            if (customBackend)
            {
                return true;
            }
            return candidate == FeedbackSuppressionMode::Standard
                       ? standardBackend != nullptr
                       : smoothBackend != nullptr;
        }

        MonitorStreamFormat format;
        std::unique_ptr<MonitorCancellationBackend> customBackend;
        std::unique_ptr<MonitorCancellationBackend> standardBackend;
        std::unique_ptr<MonitorCancellationBackend> smoothBackend;
        std::array<std::unique_ptr<webrtc::PushSincResampler>, 2>
            captureUpsamplers;
        std::array<std::unique_ptr<webrtc::PushSincResampler>, 2>
            outputDownsamplers;
        std::array<std::unique_ptr<webrtc::PushSincResampler>, 2>
            renderUpsamplers;

        std::array<std::array<float, kDeviceFrameCount>, kMonitorMaxChannels>
            inputFrame{};
        MonitorProcessingFrame processingFrame{};
        MonitorProcessingFrame renderReference{};
        std::array<std::array<float, kDeviceFrameCount>, kMonitorMaxChannels>
            processedDeviceFrame{};
        std::array<std::array<float, kOutputFifoFrames>, kMonitorMaxChannels>
            outputFifo{};

        FrequencyShifter frequencyShifter;
        FeedbackDetector detector;
        MonitorProtectionTelemetry telemetry{};
        std::size_t inputFill = 0;
        std::size_t outputRead = 0;
        std::size_t outputWrite = 0;
        std::size_t outputCount = 0;
        std::size_t warmupFrames = 0;
        std::size_t tripRampRemaining = 0;
        float gain = 1.0f;
        float tripRampGainStep = 0.0f;
        float smoothedDelayMs = 0.0f;
        MonitorProtectionState pendingTerminalState =
            MonitorProtectionState::Inactive;
        bool delayInitialized = false;
        bool outputPrimed = false;
        bool echoPathChanged = true;
        bool feedbackRisk = false;
        bool sessionActive = false;
        FeedbackSuppressionMode mode = FeedbackSuppressionMode::Standard;
    };

    InputMonitorPipeline::InputMonitorPipeline() = default;
    InputMonitorPipeline::~InputMonitorPipeline() = default;

    bool InputMonitorPipeline::prepare(
        const MonitorStreamFormat &format,
        std::unique_ptr<MonitorCancellationBackend> backend)
    {
        if (format.sampleRate != 44100 ||
            (format.inputChannels != 1 && format.inputChannels != 2) ||
            format.outputChannels != 2)
        {
            impl.reset();
            return false;
        }

        if (backend && !backend->prepare(format.inputChannels))
        {
            impl.reset();
            return false;
        }

        try
        {
            auto candidate = std::make_unique<Impl>(format);
            if (backend)
            {
                candidate->customBackend = std::move(backend);
            }
            else
            {
                try
                {
                    auto standard = std::make_unique<WebRtcAec3Backend>(
                        FeedbackSuppressionMode::Standard);
                    if (standard->prepare(format.inputChannels))
                    {
                        candidate->standardBackend = std::move(standard);
                    }
                }
                catch (...)
                {
                }
                try
                {
                    auto smooth = std::make_unique<WebRtcAec3Backend>(
                        FeedbackSuppressionMode::Smooth);
                    if (smooth->prepare(format.inputChannels))
                    {
                        candidate->smoothBackend = std::move(smooth);
                    }
                }
                catch (...)
                {
                }
            }
            impl = std::move(candidate);
        }
        catch (...)
        {
            impl.reset();
        }
        return impl != nullptr;
    }

    bool InputMonitorPipeline::isPrepared() const noexcept
    {
        return impl != nullptr;
    }

    bool InputMonitorPipeline::isModeAvailable(
        const FeedbackSuppressionMode mode) const noexcept
    {
        return impl && impl->isModeAvailable(mode);
    }

    bool InputMonitorPipeline::setFeedbackSuppressionMode(
        const FeedbackSuppressionMode mode) noexcept
    {
        if (!impl || !impl->isModeAvailable(mode))
        {
            return false;
        }
        if (impl->mode == mode)
        {
            return true;
        }
        const bool wasActive = impl->sessionActive;
        impl->mode = mode;
        impl->clearSessionBuffers();
        impl->sessionActive = wasActive;
        impl->telemetry.state = !wasActive
                                    ? MonitorProtectionState::Inactive
                                    : (mode == FeedbackSuppressionMode::Off
                                           ? MonitorProtectionState::Disabled
                                           : MonitorProtectionState::WarmingUp);
        return true;
    }

    FeedbackSuppressionMode
    InputMonitorPipeline::getFeedbackSuppressionMode() const noexcept
    {
        return impl ? impl->mode : FeedbackSuppressionMode::Standard;
    }

    void InputMonitorPipeline::startSession() noexcept
    {
        if (!impl)
        {
            return;
        }
        impl->clearSessionBuffers();
        impl->sessionActive = true;
        impl->telemetry.state = impl->mode == FeedbackSuppressionMode::Off
                                    ? MonitorProtectionState::Disabled
                                    : MonitorProtectionState::WarmingUp;
    }

    void InputMonitorPipeline::suspendSession() noexcept
    {
        if (!impl)
        {
            return;
        }
        impl->sessionActive = false;
        impl->clearSessionBuffers();
        impl->telemetry.state = MonitorProtectionState::Inactive;
    }

    MonitorProcessResult
    InputMonitorPipeline::process(const float *input, float *stereoOutput,
                                  const unsigned long frames,
                                  const AudioCallbackTiming &timing) noexcept
    {
        MonitorProcessResult result{};
        if (!impl || !impl->sessionActive || !input || !stereoOutput)
        {
            result.telemetry = getTelemetry();
            return result;
        }
        std::fill_n(stereoOutput, static_cast<std::size_t>(frames) * 2, 0.0f);
        if (timing.discontinuity)
        {
            impl->echoPathChanged = true;
        }

        for (unsigned long frame = 0; frame < frames; ++frame)
        {
            const std::size_t inputBase =
                static_cast<std::size_t>(frame) * impl->format.inputChannels;
            impl->inputFrame[0][impl->inputFill] = input[inputBase];
            impl->inputFrame[1][impl->inputFill] =
                impl->format.inputChannels == 2 ? input[inputBase + 1]
                                                : input[inputBase];
            ++impl->inputFill;
            if (impl->inputFill == kDeviceFrameCount)
            {
                impl->processDeviceFrame(timing);
                impl->inputFill = 0;
                if (impl->pendingTerminalState !=
                    MonitorProtectionState::Inactive)
                {
                    break;
                }
            }
        }

        if (!impl->outputPrimed && impl->outputCount >= kOutputPrimeFrames)
        {
            impl->outputPrimed = true;
        }
        if (impl->pendingTerminalState != MonitorProtectionState::Inactive &&
            (!impl->outputPrimed || impl->outputCount == 0))
        {
            impl->completeEmergencyStop();
            impl->outputCount = 0;
        }

        if (impl->outputPrimed)
        {
            for (unsigned long frame = 0; frame < frames; ++frame)
            {
                if (impl->outputCount == 0)
                {
                    break;
                }
                const std::size_t outputBase =
                    static_cast<std::size_t>(frame) * 2;
                stereoOutput[outputBase] =
                    impl->outputFifo[0][impl->outputRead] * impl->gain;
                stereoOutput[outputBase + 1] =
                    impl->outputFifo[1][impl->outputRead] * impl->gain;
                impl->outputRead = (impl->outputRead + 1) % kOutputFifoFrames;
                --impl->outputCount;
                result.producedOutput = true;

                if (impl->pendingTerminalState !=
                    MonitorProtectionState::Inactive)
                {
                    impl->gain =
                        std::max(0.0f, impl->gain - impl->tripRampGainStep);
                    if (impl->tripRampRemaining > 0)
                    {
                        --impl->tripRampRemaining;
                    }
                    if (impl->tripRampRemaining == 0)
                    {
                        impl->completeEmergencyStop();
                        break;
                    }
                }
                else if (impl->mode == FeedbackSuppressionMode::Smooth &&
                         impl->gain < 1.0f)
                {
                    const float fadeInStep =
                        1.0f /
                        (0.03f * static_cast<float>(impl->format.sampleRate));
                    impl->gain = std::min(1.0f, impl->gain + fadeInStep);
                }
            }
        }

        result.telemetry = impl->telemetry;
        return result;
    }

    MonitorProtectionTelemetry
    InputMonitorPipeline::getTelemetry() const noexcept
    {
        if (!impl)
        {
            return {.state = MonitorProtectionState::Unavailable};
        }
        return impl->telemetry;
    }
} // namespace cupuacu::audio
