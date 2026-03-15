#pragma once

namespace cupuacu
{
    struct AmplifyFadeSettings
    {
        double startPercent = 100.0;
        double endPercent = 100.0;
        int curveIndex = 0;
        bool lockEnabled = false;
    };

    struct NormalizeSettings
    {
    };

    struct DynamicsSettings
    {
        double thresholdPercent = 50.0;
        int ratioIndex = 1;
    };

    struct EffectSettings
    {
        AmplifyFadeSettings amplifyFade{};
        NormalizeSettings normalize{};
        DynamicsSettings dynamics{};
    };
} // namespace cupuacu
