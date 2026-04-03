#pragma once

namespace cupuacu::effects
{
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

    struct EffectSettings
    {
        AmplifyFadeSettings amplifyFade{};
        DynamicsSettings dynamics{};
        RemoveSilenceSettings removeSilence{};
    };
} // namespace cupuacu::effects
