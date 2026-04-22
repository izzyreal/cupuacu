#pragma once

#include "../Document.hpp"
#include "AudioExport.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>

#include <sndfile.h>

namespace cupuacu::file
{
    enum class MarkerNativePersistenceKind
    {
        None,
        WavCueAdtl,
        AiffMark,
    };

    struct MarkerNativePersistenceCapabilities
    {
        MarkerNativePersistenceKind kind = MarkerNativePersistenceKind::None;
        bool preservesFramePosition = false;
        bool preservesLabel = false;
        std::optional<std::size_t> maxLabelBytes;

        [[nodiscard]] bool supportsNativeMarkers() const
        {
            return kind != MarkerNativePersistenceKind::None;
        }
    };

    enum class MarkerPersistenceFidelity
    {
        Exact,
        Lossy,
        Unsupported,
    };

    struct MarkerPersistenceAssessment
    {
        MarkerNativePersistenceCapabilities capabilities;
        MarkerPersistenceFidelity fidelity = MarkerPersistenceFidelity::Unsupported;
        bool requiresSidecarOrSessionFallback = false;
        std::string summary;
    };

    inline MarkerNativePersistenceCapabilities
    markerNativePersistenceCapabilitiesForSettings(
        const AudioExportSettings &settings)
    {
        if (settings.majorFormat == SF_FORMAT_WAV &&
            settings.codec == AudioExportCodec::PCM)
        {
            return {
                .kind = MarkerNativePersistenceKind::WavCueAdtl,
                .preservesFramePosition = true,
                .preservesLabel = true,
                .maxLabelBytes = std::nullopt,
            };
        }

        if (settings.majorFormat == SF_FORMAT_AIFF &&
            settings.codec == AudioExportCodec::PCM)
        {
            return {
                .kind = MarkerNativePersistenceKind::AiffMark,
                .preservesFramePosition = true,
                .preservesLabel = true,
                .maxLabelBytes = 255,
            };
        }

        return {};
    }

    inline MarkerPersistenceAssessment assessMarkerPersistenceForSettings(
        const Document &document, const AudioExportSettings &settings)
    {
        const auto capabilities =
            markerNativePersistenceCapabilitiesForSettings(settings);

        if (!capabilities.supportsNativeMarkers())
        {
            return {
                .capabilities = capabilities,
                .fidelity = MarkerPersistenceFidelity::Unsupported,
                .requiresSidecarOrSessionFallback =
                    !document.getMarkers().empty(),
                .summary =
                    document.getMarkers().empty()
                        ? "Selected target format has no native marker writer."
                        : "Selected target format has no native marker writer; markers need Cupuacu fallback persistence.",
            };
        }

        if (!capabilities.maxLabelBytes.has_value())
        {
            return {
                .capabilities = capabilities,
                .fidelity = MarkerPersistenceFidelity::Exact,
                .requiresSidecarOrSessionFallback = false,
                .summary = "All current marker fields map to native format metadata.",
            };
        }

        for (const auto &marker : document.getMarkers())
        {
            if (marker.label.size() > *capabilities.maxLabelBytes)
            {
                return {
                    .capabilities = capabilities,
                    .fidelity = MarkerPersistenceFidelity::Lossy,
                    .requiresSidecarOrSessionFallback = true,
                    .summary =
                        "Native marker metadata is available, but at least one marker label exceeds the format limit.",
                };
            }
        }

        return {
            .capabilities = capabilities,
            .fidelity = MarkerPersistenceFidelity::Exact,
            .requiresSidecarOrSessionFallback = false,
            .summary = "All current marker fields map to native format metadata.",
        };
    }
} // namespace cupuacu::file
