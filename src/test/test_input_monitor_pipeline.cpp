#include <catch2/catch_test_macros.hpp>

#include "audio/InputMonitorPipeline.hpp"
#include "audio/WebRtcAec3Backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

namespace
{
    class PassthroughCancellationBackend final
        : public cupuacu::audio::MonitorCancellationBackend
    {
    public:
        bool prepare(const uint8_t channels) override
        {
            captureChannels = channels;
            return channels == 1 || channels == 2;
        }

        bool process(cupuacu::audio::MonitorProcessingFrame &,
                     const cupuacu::audio::MonitorProcessingFrame &,
                     const int delayMs, const bool, const bool,
                     cupuacu::audio::MonitorCancellationMetrics
                         &metrics) noexcept override
        {
            metrics.active = true;
            metrics.delayMs = delayMs;
            return captureChannels != 0;
        }

    private:
        uint8_t captureChannels = 0;
    };

    class FailingCancellationBackend final
        : public cupuacu::audio::MonitorCancellationBackend
    {
    public:
        bool prepare(const uint8_t channels) override
        {
            return channels == 1 || channels == 2;
        }

        bool
        process(cupuacu::audio::MonitorProcessingFrame &,
                const cupuacu::audio::MonitorProcessingFrame &, int, bool, bool,
                cupuacu::audio::MonitorCancellationMetrics &) noexcept override
        {
            return successfulFrames++ < 2;
        }

    private:
        int successfulFrames = 0;
    };

    class CountingCancellationBackend final
        : public cupuacu::audio::MonitorCancellationBackend
    {
    public:
        bool prepare(const uint8_t channels) override
        {
            return channels == 1 || channels == 2;
        }

        bool
        process(cupuacu::audio::MonitorProcessingFrame &,
                const cupuacu::audio::MonitorProcessingFrame &, int, bool, bool,
                cupuacu::audio::MonitorCancellationMetrics &) noexcept override
        {
            ++processCount;
            return true;
        }

        int processCount = 0;
    };

    cupuacu::audio::MonitorStreamFormat stereoFormat()
    {
        return {.sampleRate = 44100,
                .callbackFrames = 256,
                .inputChannels = 2,
                .outputChannels = 2,
                .inputLatencySeconds = 0.005,
                .outputLatencySeconds = 0.005};
    }
} // namespace

TEST_CASE("Input monitor pipeline rejects unsupported stream formats",
          "[audio][monitor]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    auto format = stereoFormat();
    format.sampleRate = 48000;
    REQUIRE_FALSE(pipeline.prepare(
        format, std::make_unique<PassthroughCancellationBackend>()));

    format = stereoFormat();
    format.inputChannels = 0;
    REQUIRE_FALSE(pipeline.prepare(
        format, std::make_unique<PassthroughCancellationBackend>()));
}

TEST_CASE("Input monitor pipeline primes before producing protected output",
          "[audio][monitor]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    REQUIRE(pipeline.prepare(
        stereoFormat(), std::make_unique<PassthroughCancellationBackend>()));
    pipeline.startSession();

    std::vector<float> input(256 * 2, 0.25f);
    std::vector<float> output(256 * 2, 0.0f);
    bool produced = false;
    for (int callback = 0; callback < 5; ++callback)
    {
        std::fill(output.begin(), output.end(), 0.0f);
        const auto result =
            pipeline.process(input.data(), output.data(), 256, {});
        produced |= result.producedOutput;
    }

    REQUIRE(produced);
    REQUIRE(std::any_of(output.begin(), output.end(),
                        [](const float sample)
                        {
                            return sample != 0.0f;
                        }));
    REQUIRE(pipeline.getTelemetry().state ==
            cupuacu::audio::MonitorProtectionState::Active);
    REQUIRE(pipeline.getTelemetry().estimatedDelayMs >= 10);
}

TEST_CASE("Input monitor pipeline fades in without a startup discontinuity",
          "[audio][monitor]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    REQUIRE(pipeline.prepare(
        stereoFormat(), std::make_unique<PassthroughCancellationBackend>()));
    REQUIRE(pipeline.setFeedbackSuppressionMode(
        cupuacu::audio::FeedbackSuppressionMode::Smooth));
    pipeline.startSession();

    std::vector<float> input(256 * 2, 0.5f);
    std::vector<float> output(256 * 2, 0.0f);
    bool foundFirstOutput = false;
    float previous = 0.0f;
    float maximumStep = 0.0f;
    for (int callback = 0; callback < 8; ++callback)
    {
        std::fill(output.begin(), output.end(), 0.0f);
        const auto result =
            pipeline.process(input.data(), output.data(), 256, {});
        if (!result.producedOutput)
        {
            continue;
        }

        for (std::size_t frame = 0; frame < 256; ++frame)
        {
            const float sample = output[frame * 2];
            maximumStep = std::max(maximumStep, std::abs(sample - previous));
            previous = sample;
        }
        foundFirstOutput = true;
    }

    REQUIRE(foundFirstOutput);
    REQUIRE(maximumStep < 0.02f);
    REQUIRE(previous > 0.45f);
}

TEST_CASE("Input monitor suppression modes switch live and Off bypasses AEC",
          "[audio][monitor]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    auto backend = std::make_unique<CountingCancellationBackend>();
    auto *backendObserver = backend.get();
    REQUIRE(pipeline.prepare(stereoFormat(), std::move(backend)));
    REQUIRE(pipeline.getFeedbackSuppressionMode() ==
            cupuacu::audio::FeedbackSuppressionMode::Standard);
    REQUIRE(
        pipeline.isModeAvailable(cupuacu::audio::FeedbackSuppressionMode::Off));
    REQUIRE(pipeline.isModeAvailable(
        cupuacu::audio::FeedbackSuppressionMode::Smooth));
    pipeline.startSession();

    std::vector<float> input(441 * 2, 0.25f);
    std::vector<float> output(441 * 2, 0.0f);
    for (int callback = 0; callback < 4; ++callback)
    {
        pipeline.process(input.data(), output.data(), 441, {});
    }
    REQUIRE(backendObserver->processCount > 0);

    REQUIRE(pipeline.setFeedbackSuppressionMode(
        cupuacu::audio::FeedbackSuppressionMode::Smooth));
    REQUIRE(pipeline.getTelemetry().state ==
            cupuacu::audio::MonitorProtectionState::WarmingUp);

    REQUIRE(pipeline.setFeedbackSuppressionMode(
        cupuacu::audio::FeedbackSuppressionMode::Off));
    REQUIRE(pipeline.getTelemetry().state ==
            cupuacu::audio::MonitorProtectionState::Disabled);
    const int protectedProcessCount = backendObserver->processCount;

    input.assign(input.size(), 1.0f);
    cupuacu::audio::MonitorProcessResult result{};
    for (int callback = 0; callback < 5; ++callback)
    {
        result = pipeline.process(input.data(), output.data(), 441, {});
    }

    REQUIRE(result.producedOutput);
    REQUIRE(result.telemetry.state ==
            cupuacu::audio::MonitorProtectionState::Disabled);
    REQUIRE(backendObserver->processCount == protectedProcessCount);
}

TEST_CASE("Input monitor pipeline preserves a true stereo protected path",
          "[audio][monitor]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    REQUIRE(pipeline.prepare(
        stereoFormat(), std::make_unique<PassthroughCancellationBackend>()));
    pipeline.startSession();

    std::vector<float> input(441 * 2);
    for (std::size_t frame = 0; frame < 441; ++frame)
    {
        input[frame * 2] = 0.2f;
        input[frame * 2 + 1] = -0.4f;
    }
    std::vector<float> output(441 * 2, 0.0f);
    cupuacu::audio::MonitorProcessResult result{};
    for (int callback = 0; callback < 4; ++callback)
    {
        result = pipeline.process(input.data(), output.data(), 441, {});
    }

    REQUIRE(result.producedOutput);
    float rightPeak = 0.0f;
    float leftPeak = 0.0f;
    for (std::size_t frame = 0; frame < 441; ++frame)
    {
        leftPeak = std::max(leftPeak, std::abs(output[frame * 2]));
        rightPeak = std::max(rightPeak, std::abs(output[frame * 2 + 1]));
    }
    REQUIRE(leftPeak > 0.1f);
    REQUIRE(rightPeak > leftPeak * 1.5f);
}

TEST_CASE("Input monitor pipeline trips and latches on sustained clipping",
          "[audio][monitor]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    REQUIRE(pipeline.prepare(
        stereoFormat(), std::make_unique<PassthroughCancellationBackend>()));
    pipeline.startSession();

    std::vector<float> input(441 * 2, 1.0f);
    std::vector<float> output(441 * 2, 0.0f);
    cupuacu::audio::MonitorProcessResult result{};
    for (int frame = 0; frame < 4; ++frame)
    {
        result = pipeline.process(input.data(), output.data(), 441, {});
        if (result.telemetry.state ==
            cupuacu::audio::MonitorProtectionState::Tripped)
        {
            break;
        }
    }

    REQUIRE(result.telemetry.state ==
            cupuacu::audio::MonitorProtectionState::Tripped);
    REQUIRE(result.telemetry.tripGeneration == 1);
    REQUIRE(result.producedOutput);
    REQUIRE(std::any_of(output.begin(), output.begin() + 441,
                        [](const float sample)
                        {
                            return std::abs(sample) > 0.1f;
                        }));
    REQUIRE(std::all_of(output.end() - 200, output.end(),
                        [](const float sample)
                        {
                            return sample == 0.0f;
                        }));

    std::fill(output.begin(), output.end(), 0.0f);
    REQUIRE_FALSE(
        pipeline.process(input.data(), output.data(), 441, {}).producedOutput);
    REQUIRE(std::all_of(output.begin(), output.end(),
                        [](const float sample)
                        {
                            return sample == 0.0f;
                        }));
}

TEST_CASE("Input monitor pipeline fades safely if cancellation fails",
          "[audio][monitor]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    REQUIRE(pipeline.prepare(stereoFormat(),
                             std::make_unique<FailingCancellationBackend>()));
    pipeline.startSession();

    std::vector<float> input(441 * 2, 0.25f);
    std::vector<float> output(441 * 2, 0.0f);
    pipeline.process(input.data(), output.data(), 441, {});
    pipeline.process(input.data(), output.data(), 441, {});
    const auto result = pipeline.process(input.data(), output.data(), 441, {});

    REQUIRE(result.telemetry.state ==
            cupuacu::audio::MonitorProtectionState::Unavailable);
    REQUIRE(result.telemetry.tripGeneration == 1);
    REQUIRE(result.producedOutput);
    REQUIRE(std::all_of(output.end() - 200, output.end(),
                        [](const float sample)
                        {
                            return sample == 0.0f;
                        }));
    REQUIRE_FALSE(
        pipeline.process(input.data(), output.data(), 441, {}).producedOutput);
}

TEST_CASE("WebRTC AEC3 monitor backend processes stereo callback fragments",
          "[audio][monitor][aec3]")
{
    cupuacu::audio::InputMonitorPipeline pipeline;
    REQUIRE(pipeline.prepare(stereoFormat()));
    pipeline.startSession();

    std::vector<float> input(256 * 2);
    std::vector<float> output(256 * 2, 0.0f);
    for (std::size_t frame = 0; frame < 256; ++frame)
    {
        input[frame * 2] = frame % 17 == 0 ? 0.2f : 0.0f;
        input[frame * 2 + 1] = frame % 23 == 0 ? -0.2f : 0.0f;
    }

    cupuacu::audio::MonitorProcessResult result{};
    for (int callback = 0; callback < 5; ++callback)
    {
        result = pipeline.process(input.data(), output.data(), 256, {});
    }

    REQUIRE(result.producedOutput);
    REQUIRE(result.telemetry.state ==
            cupuacu::audio::MonitorProtectionState::Active);
    REQUIRE(std::all_of(output.begin(), output.end(),
                        [](const float sample)
                        {
                            return std::isfinite(sample);
                        }));
}

TEST_CASE("WebRTC AEC3 attenuates a deterministic delayed acoustic return",
          "[audio][monitor][aec3]")
{
    cupuacu::audio::WebRtcAec3Backend backend;
    REQUIRE(backend.prepare(1));

    constexpr std::size_t delayFrames = 3;
    constexpr int totalFrames = 500;
    std::array<cupuacu::audio::MonitorProcessingFrame, delayFrames + 1>
        renderHistory{};
    std::minstd_rand generator{0xC0FFEE};
    std::uniform_real_distribution<float> noise{-0.25f, 0.25f};

    double inputPower = 0.0;
    double outputPower = 0.0;
    std::size_t measuredSamples = 0;
    for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
    {
        cupuacu::audio::MonitorProcessingFrame render{};
        for (std::size_t sample = 0;
             sample < cupuacu::audio::kMonitorProcessingFrameCount; ++sample)
        {
            render[0][sample] = noise(generator);
            render[1][sample] = noise(generator);
        }
        renderHistory[static_cast<std::size_t>(frameIndex) %
                      renderHistory.size()] = render;

        cupuacu::audio::MonitorProcessingFrame capture{};
        if (frameIndex >= static_cast<int>(delayFrames))
        {
            const auto &echoSource =
                renderHistory[(static_cast<std::size_t>(frameIndex) -
                               delayFrames) %
                              renderHistory.size()];
            for (std::size_t sample = 0;
                 sample < cupuacu::audio::kMonitorProcessingFrameCount;
                 ++sample)
            {
                capture[0][sample] = echoSource[0][sample] * 0.65f;
            }
        }
        const auto uncancelled = capture[0];

        cupuacu::audio::MonitorCancellationMetrics metrics{};
        REQUIRE(backend.process(capture, render, 30, frameIndex == 0, false,
                                metrics));

        if (frameIndex >= 300)
        {
            for (std::size_t sample = 0;
                 sample < cupuacu::audio::kMonitorProcessingFrameCount;
                 ++sample)
            {
                inputPower += static_cast<double>(uncancelled[sample]) *
                              uncancelled[sample];
                outputPower += static_cast<double>(capture[0][sample]) *
                               capture[0][sample];
                ++measuredSamples;
            }
        }
    }

    REQUIRE(measuredSamples > 0);
    const double attenuationDb =
        10.0 * std::log10(inputPower / std::max(outputPower, 1.0e-18));
    REQUIRE(attenuationDb >= 12.0);
}

TEST_CASE("WebRTC AEC3 raises stable closed-loop gain by at least six dB",
          "[audio][monitor][aec3]")
{
    auto runLoop = [](const bool protectedPath)
    {
        cupuacu::audio::WebRtcAec3Backend backend;
        if (protectedPath)
        {
            REQUIRE(backend.prepare(1));
        }

        constexpr std::size_t acousticDelayFrames = 3;
        constexpr int trainingFrames = 400;
        constexpr int measuredFrames = 120;
        std::array<cupuacu::audio::MonitorProcessingFrame,
                   acousticDelayFrames + 1>
            outputHistory{};
        std::minstd_rand generator{0x51AB1E};
        std::uniform_real_distribution<float> noise{-0.004f, 0.004f};
        double measuredPower = 0.0;
        float measuredPeak = 0.0f;

        for (int frameIndex = 0; frameIndex < trainingFrames + measuredFrames;
             ++frameIndex)
        {
            const auto previousOutput =
                outputHistory[(static_cast<std::size_t>(frameIndex) +
                               outputHistory.size() - 1) %
                              outputHistory.size()];
            const auto &echoSource =
                outputHistory[(static_cast<std::size_t>(frameIndex) +
                               outputHistory.size() - acousticDelayFrames) %
                              outputHistory.size()];
            const float loopGain = frameIndex < trainingFrames ? 0.5f : 2.0f;

            cupuacu::audio::MonitorProcessingFrame capture{};
            for (std::size_t sample = 0;
                 sample < cupuacu::audio::kMonitorProcessingFrameCount;
                 ++sample)
            {
                capture[0][sample] = std::clamp(
                    noise(generator) + loopGain * echoSource[0][sample], -1.0f,
                    1.0f);
            }

            if (protectedPath)
            {
                cupuacu::audio::MonitorCancellationMetrics metrics{};
                REQUIRE(backend.process(capture, previousOutput, 20,
                                        frameIndex == 0, false, metrics));
            }

            outputHistory[static_cast<std::size_t>(frameIndex) %
                          outputHistory.size()] = capture;
            if (frameIndex >= trainingFrames)
            {
                for (const float sample : capture[0])
                {
                    measuredPower += static_cast<double>(sample) * sample;
                    measuredPeak = std::max(measuredPeak, std::abs(sample));
                }
            }
        }

        return std::pair{
            std::sqrt(measuredPower /
                      static_cast<double>(
                          measuredFrames *
                          cupuacu::audio::kMonitorProcessingFrameCount)),
            measuredPeak};
    };

    const auto unprotected = runLoop(false);
    const auto protectedResult = runLoop(true);
    REQUIRE(unprotected.second >= 0.99f);
    REQUIRE(protectedResult.second < 0.5f);
    REQUIRE(protectedResult.first < unprotected.first * 0.5);
}

TEST_CASE("WebRTC AEC3 releases after prolonged closed-loop excitation",
          "[audio][monitor][aec3]")
{
    cupuacu::audio::WebRtcAec3Backend backend;
    REQUIRE(backend.prepare(1));

    constexpr std::size_t acousticDelayFrames = 3;
    constexpr int drivenFrames = 500;
    constexpr int releaseFrames = 500;
    std::array<cupuacu::audio::MonitorProcessingFrame, acousticDelayFrames + 1>
        outputHistory{};
    std::minstd_rand generator{0xA17FACE};
    std::uniform_real_distribution<float> excitation{-0.08f, 0.08f};

    double releasePower = 0.0;
    double releaseInputPower = 0.0;
    float releasePeak = 0.0f;
    std::size_t releaseSamples = 0;
    cupuacu::audio::MonitorCancellationMetrics finalMetrics{};
    for (int frameIndex = 0; frameIndex < drivenFrames + releaseFrames;
         ++frameIndex)
    {
        const auto previousOutput =
            outputHistory[(static_cast<std::size_t>(frameIndex) +
                           outputHistory.size() - 1) %
                          outputHistory.size()];
        const auto &echoSource =
            outputHistory[(static_cast<std::size_t>(frameIndex) +
                           outputHistory.size() - acousticDelayFrames) %
                          outputHistory.size()];

        cupuacu::audio::MonitorProcessingFrame capture{};
        for (std::size_t sample = 0;
             sample < cupuacu::audio::kMonitorProcessingFrameCount; ++sample)
        {
            const float source =
                frameIndex < drivenFrames ? excitation(generator) : 0.0f;
            capture[0][sample] =
                std::clamp(source + 1.5f * echoSource[0][sample], -1.0f, 1.0f);
        }

        cupuacu::audio::MonitorCancellationMetrics metrics{};
        if (frameIndex >= drivenFrames + releaseFrames - 100)
        {
            for (const float sample : capture[0])
            {
                releaseInputPower += static_cast<double>(sample) * sample;
            }
        }
        const bool feedbackRisk =
            frameIndex >= drivenFrames - 50 && frameIndex < drivenFrames;
        REQUIRE(backend.process(capture, previousOutput, 20, frameIndex == 0,
                                feedbackRisk, metrics));
        finalMetrics = metrics;
        outputHistory[static_cast<std::size_t>(frameIndex) %
                      outputHistory.size()] = capture;

        if (frameIndex >= drivenFrames + releaseFrames - 100)
        {
            for (const float sample : capture[0])
            {
                releasePower += static_cast<double>(sample) * sample;
                releasePeak = std::max(releasePeak, std::abs(sample));
                ++releaseSamples;
            }
        }
    }

    REQUIRE(releaseSamples > 0);
    const double releaseRms =
        std::sqrt(releasePower / static_cast<double>(releaseSamples));
    const double releaseInputRms =
        std::sqrt(releaseInputPower / static_cast<double>(releaseSamples));
    CAPTURE(releaseRms, releaseInputRms, releasePeak,
            finalMetrics.echoReturnLossDb,
            finalMetrics.echoReturnLossEnhancementDb);
    REQUIRE(releaseRms < 0.001);
    REQUIRE(releasePeak < 0.01f);
}

TEST_CASE("WebRTC AEC3 tail control preserves quiet input without a render",
          "[audio][monitor][aec3]")
{
    cupuacu::audio::WebRtcAec3Backend backend;
    REQUIRE(backend.prepare(1));

    constexpr int totalFrames = 300;
    double inputPower = 0.0;
    double outputPower = 0.0;
    std::size_t measuredSamples = 0;
    float phase = 0.0f;
    for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
    {
        cupuacu::audio::MonitorProcessingFrame capture{};
        cupuacu::audio::MonitorProcessingFrame render{};
        for (std::size_t sample = 0;
             sample < cupuacu::audio::kMonitorProcessingFrameCount; ++sample)
        {
            capture[0][sample] = 0.002f * std::sin(phase);
            phase += 2.0f * std::numbers::pi_v<float> * 440.0f /
                     static_cast<float>(
                         cupuacu::audio::kMonitorProcessingSampleRate);
        }
        const auto original = capture[0];

        cupuacu::audio::MonitorCancellationMetrics metrics{};
        REQUIRE(backend.process(capture, render, 20, frameIndex == 0, false,
                                metrics));
        if (frameIndex >= 200)
        {
            for (std::size_t sample = 0;
                 sample < cupuacu::audio::kMonitorProcessingFrameCount;
                 ++sample)
            {
                inputPower +=
                    static_cast<double>(original[sample]) * original[sample];
                outputPower += static_cast<double>(capture[0][sample]) *
                               capture[0][sample];
                ++measuredSamples;
            }
        }
    }

    REQUIRE(measuredSamples > 0);
    REQUIRE(outputPower > inputPower * 0.8);
}
