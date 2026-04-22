#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "MarkerPersistence.hpp"
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
        std::optional<MarkerPersistenceAssessment> markerPersistence;
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
            const auto markerPersistence =
                state == nullptr
                    ? std::optional<MarkerPersistenceAssessment>(std::nullopt)
                    : std::optional<MarkerPersistenceAssessment>(
                          cupuacu::file::assessMarkerPersistenceForSettings(
                              state->getActiveDocumentSession().document,
                              settings));
            if (!preservation.available)
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason = preservation.reason,
                        .markerPersistence = markerPersistence};
            }

            return {.mode = SaveWriteMode::OverwritePreservingRewrite,
                    .markerPersistence = markerPersistence};
        }

        [[nodiscard]] static SaveWritePlan
        planPreservingSaveAs(const cupuacu::State *state,
                             const AudioExportSettings &settings)
        {
            if (state == nullptr)
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason = "State is null",
                        .markerPersistence = std::nullopt};
            }

            const auto support =
                cupuacu::file::assessPreservationAgainstReference(state, settings);
            const auto markerPersistence =
                cupuacu::file::assessMarkerPersistenceForSettings(
                    state->getActiveDocumentSession().document, settings);
            if (!support.available)
            {
                return {.mode = SaveWriteMode::PreservationRequiredButUnavailable,
                        .preservationUnavailableReason = support.reason,
                        .markerPersistence = markerPersistence};
            }

            return {.mode = SaveWriteMode::OverwritePreservingRewrite,
                    .markerPersistence = markerPersistence};
        }
    };
} // namespace cupuacu::file
