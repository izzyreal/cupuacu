#include "DynamicsEffect.hpp"

namespace cupuacu::effects
{
    namespace
    {
        EffectDialogDefinition<DynamicsSettings> makeDynamicsDefinition()
        {
            EffectDialogDefinition<DynamicsSettings> definition{};
            definition.title = "Dynamics";
            definition.loadSettings =
                [](cupuacu::State *state)
            {
                return state->effectSettings.dynamics;
            };
            definition.saveSettings =
                [](cupuacu::State *state, const DynamicsSettings &settings)
            {
                state->effectSettings.dynamics = settings;
            };
            definition.applySettings =
                [](cupuacu::State *state, const DynamicsSettings &settings)
            {
                performDynamics(state, settings);
            };

            definition.parameters.push_back(
                EffectParameterSpec<DynamicsSettings>::percent(
                    "threshold", "Threshold", 0.0, 100.0,
                    [](const DynamicsSettings &settings)
                    {
                        return settings.thresholdPercent;
                    },
                    [](DynamicsSettings &settings, const double value)
                    {
                        settings.thresholdPercent = std::clamp(value, 0.0, 100.0);
                    }));
            definition.parameters.push_back(
                EffectParameterSpec<DynamicsSettings>::enumeration(
                    "ratio", "Ratio", {"2:1", "4:1", "8:1", "Limiter"},
                    [](const DynamicsSettings &settings)
                    {
                        return settings.ratioIndex;
                    },
                    [](DynamicsSettings &settings, const int index)
                    {
                        settings.ratioIndex = std::clamp(index, 0, 3);
                    }));
            definition.actions.push_back(
                {"Reset",
                 [](DynamicsSettings &settings, cupuacu::State *)
                 {
                     settings.thresholdPercent = 50.0;
                     settings.ratioIndex = 1;
                 }});
            return definition;
        }
    } // namespace

    DynamicsDialog::DynamicsDialog(cupuacu::State *stateToUse)
    {
        dialog = std::make_unique<EffectDialogWindow<DynamicsSettings>>(
            stateToUse, makeDynamicsDefinition(), 480, 220);
    }
} // namespace cupuacu::effects
