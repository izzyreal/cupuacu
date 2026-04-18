#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "OverwritePreservation.hpp"
#include "wav/WavPreservationSupport.hpp"

#include <optional>
#include <string>

namespace cupuacu::file
{
    enum class SaveWriteMode
    {
        GenericRewrite,
        OverwritePreservingRewrite,
        PreservationRequiredButUnavailable,
    };

    struct SaveWritePlan
    {
        SaveWriteMode mode = SaveWriteMode::GenericRewrite;
        std::optional<std::string> preservationUnavailableReason;
    };

    class SaveWritePlanner
    {
    public:
        [[nodiscard]] static SaveWritePlan
        planPreservingOverwrite(const cupuacu::State *state,
                                const AudioExportSettings &settings)
        {
            if (!isOverwritePreservingWavRewriteCandidate(settings))
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason =
                            "Current file is not a WAV PCM16 preservation candidate"};
            }

            const auto preservation =
                cupuacu::file::OverwritePreservation::assessActiveSession(state);
            if (!preservation.available)
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason = preservation.reason};
            }

            return {.mode = SaveWriteMode::OverwritePreservingRewrite};
        }

        [[nodiscard]] static SaveWritePlan
        planPreservingSaveAs(const cupuacu::State *state,
                             const AudioExportSettings &settings)
        {
            if (state == nullptr)
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason = "State is null"};
            }

            const auto support =
                cupuacu::file::wav::WavPreservationSupport::assessAgainstReference(
                    state, settings);
            if (!support.supported)
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason = support.reason};
            }

            return {.mode = SaveWriteMode::OverwritePreservingRewrite};
        }
    };
} // namespace cupuacu::file
