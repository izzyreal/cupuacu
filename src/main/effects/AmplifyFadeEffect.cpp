#include "AmplifyFadeEffect.hpp"

namespace cupuacu::effects
{
    namespace
    {
        EffectDialogDefinition<AmplifyFadeSettings>
        makeAmplifyFadeDefinition()
        {
            EffectDialogDefinition<AmplifyFadeSettings> definition{};
            definition.title = "Amplify/Fade";
            definition.loadSettings =
                [](cupuacu::State *state)
            {
                return state->effectSettings.amplifyFade;
            };
            definition.saveSettings =
                [](cupuacu::State *state, const AmplifyFadeSettings &settings)
            {
                state->effectSettings.amplifyFade = settings;
            };
            definition.applySettings =
                [](cupuacu::State *state, const AmplifyFadeSettings &settings)
            {
                performAmplifyFade(state, settings);
            };
            definition.createPreviewSession =
                [](cupuacu::State *, const AmplifyFadeSettings &settings)
            {
                return std::make_shared<AmplifyFadePreviewSession>(settings);
            };

            definition.parameters.push_back(
                EffectParameterSpec<AmplifyFadeSettings>::percent(
                    "start", "Start", 0.0, 1000.0,
                    [](const AmplifyFadeSettings &settings)
                    {
                        return settings.startPercent;
                    },
                    [](AmplifyFadeSettings &settings, const double value)
                    {
                        settings.startPercent = std::clamp(value, 0.0, 1000.0);
                        if (settings.lockEnabled)
                        {
                            settings.endPercent = settings.startPercent;
                        }
                    }));
            definition.parameters.push_back(
                EffectParameterSpec<AmplifyFadeSettings>::percent(
                    "end", "End", 0.0, 1000.0,
                    [](const AmplifyFadeSettings &settings)
                    {
                        return settings.endPercent;
                    },
                    [](AmplifyFadeSettings &settings, const double value)
                    {
                        settings.endPercent = std::clamp(value, 0.0, 1000.0);
                        if (settings.lockEnabled)
                        {
                            settings.startPercent = settings.endPercent;
                        }
                    }));
            definition.parameters.push_back(
                EffectParameterSpec<AmplifyFadeSettings>::toggle(
                    "lock", "Lock",
                    [](const AmplifyFadeSettings &settings)
                    {
                        return settings.lockEnabled;
                    },
                    [](AmplifyFadeSettings &settings, const bool enabled)
                    {
                        settings.lockEnabled = enabled;
                        if (enabled)
                        {
                            settings.endPercent = settings.startPercent;
                        }
                    }));
            definition.parameters.push_back(
                EffectParameterSpec<AmplifyFadeSettings>::enumeration(
                    "curve", "Curve",
                    {"Linear", "Exponential", "Logarithmic"},
                    [](const AmplifyFadeSettings &settings)
                    {
                        return settings.curveIndex;
                    },
                    [](AmplifyFadeSettings &settings, const int index)
                    {
                        settings.curveIndex = std::clamp(index, 0, 2);
                    }));

            definition.actions.push_back(
                {"Reset",
                 [](AmplifyFadeSettings &settings, cupuacu::State *)
                 {
                     settings.startPercent = 100.0;
                     settings.endPercent = 100.0;
                     settings.curveIndex = 0;
                 }});
            definition.actions.push_back(
                {"Normalize",
                 [](AmplifyFadeSettings &settings, cupuacu::State *state)
                 {
                     const double normalizePercent =
                         computeNormalizePercent(state);
                     settings.startPercent = normalizePercent;
                     settings.endPercent = normalizePercent;
                 }});
            definition.actions.push_back(
                {"Fade in",
                 [](AmplifyFadeSettings &settings, cupuacu::State *)
                 {
                     settings.startPercent = 0.0;
                     settings.endPercent = 100.0;
                 }});
            definition.actions.push_back(
                {"Fade out",
                 [](AmplifyFadeSettings &settings, cupuacu::State *)
                 {
                     settings.startPercent = 100.0;
                     settings.endPercent = 0.0;
                 }});

            return definition;
        }
    } // namespace

    AmplifyFadeDialog::AmplifyFadeDialog(cupuacu::State *stateToUse)
    {
        dialog = std::make_unique<EffectDialogWindow<AmplifyFadeSettings>>(
            stateToUse, makeAmplifyFadeDefinition(), 560, 320);
    }
} // namespace cupuacu::effects
