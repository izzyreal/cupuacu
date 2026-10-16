#pragma once

#include <vector>

namespace cupuacu::effects
{
    enum class PitchUnit
    {
        Percent,
        Semitones
    };
    enum class PitchType
    {
        Resample,
        Stretch
    };

    struct PitchSettings
    {
        double factor = 1.0;
        PitchUnit unit = PitchUnit::Percent;
        PitchType type = PitchType::Resample;
    };

    struct AmplifyFadeSettings
    {
        double startPercent = 100.0;
        double endPercent = 100.0;
        int curveIndex = 0;
        bool lockEnabled = false;
    };

    struct DynamicsSettings
    {
        double thresholdPercent = 50.0;
        int ratioIndex = 1;
    };

    struct RemoveSilenceSettings
    {
        int modeIndex = 0;
        int thresholdUnitIndex = 0;
        double thresholdDb = -48.0;
        double thresholdSampleValue = 0.0;
        double minimumSilenceLengthMs = 10.0;
    };

    struct AmplifyEnvelopePoint
    {
        double position = 0.0;
        double percent = 100.0;
    };

    struct AmplifyEnvelopeSettings
    {
        std::vector<AmplifyEnvelopePoint> points{
            AmplifyEnvelopePoint{0.0, 100.0}, AmplifyEnvelopePoint{1.0, 100.0}};
        bool snapEnabled = false;
        double fadeLengthMs = 100.0;
    };

    struct EffectSettings
    {
        PitchSettings pitch{};
        AmplifyFadeSettings amplifyFade{};
        AmplifyEnvelopeSettings amplifyEnvelope{};
        DynamicsSettings dynamics{};
        RemoveSilenceSettings removeSilence{};
    };
} // namespace cupuacu::effects
