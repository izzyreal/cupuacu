#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "OverwritePreservation.hpp"
#include "PreservationBackend.hpp"

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
                cupuacu::file::assessPreservationAgainstReference(state, settings);
            if (!support.available)
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason = support.reason};
            }

            return {.mode = SaveWriteMode::OverwritePreservingRewrite};
        }
    };
} // namespace cupuacu::file
