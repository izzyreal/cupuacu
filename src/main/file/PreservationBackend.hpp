#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "OverwritePreservationState.hpp"
#include "wav/WavPreservationSupport.hpp"
#include "wav/WavPreservationWriter.hpp"

#include <filesystem>
#include <stdexcept>

namespace cupuacu::file
{
    enum class PreservationBackendKind
    {
        None,
        WavPcm16,
    };

    inline PreservationBackendKind
    preservationBackendKindForSettings(const AudioExportSettings &settings)
    {
        if (isOverwritePreservingWavRewriteCandidate(settings))
        {
            return PreservationBackendKind::WavPcm16;
        }

        return PreservationBackendKind::None;
    }

    inline OverwritePreservationState
    assessPreservationAgainstReference(const cupuacu::State *state,
                                       const AudioExportSettings &settings)
    {
        if (state == nullptr)
        {
            return {.available = false, .reason = "State is null"};
        }

        switch (preservationBackendKindForSettings(settings))
        {
            case PreservationBackendKind::WavPcm16:
            {
                const auto support =
                    cupuacu::file::wav::WavPreservationSupport::
                        assessAgainstReference(state, settings);
                return {.available = support.supported, .reason = support.reason};
            }
            case PreservationBackendKind::None:
            default:
                return {.available = false,
                        .reason = "Selected target format does not have a preservation writer yet"};
        }
    }

    inline OverwritePreservationState
    assessPreservationOverwrite(const cupuacu::State *state,
                                const AudioExportSettings &settings)
    {
        if (state == nullptr)
        {
            return {.available = false, .reason = "State is null"};
        }

        switch (preservationBackendKindForSettings(settings))
        {
            case PreservationBackendKind::WavPcm16:
            {
                const auto support =
                    cupuacu::file::wav::WavPreservationSupport::assessOverwrite(
                        state);
                return {.available = support.supported, .reason = support.reason};
            }
            case PreservationBackendKind::None:
            default:
                return {.available = false,
                        .reason = "Current file format does not have a preservation writer yet"};
        }
    }

    inline void writePreservingFile(
        cupuacu::State *state, const std::filesystem::path &referencePath,
        const std::filesystem::path &outputPath,
        const AudioExportSettings &settings)
    {
        if (state == nullptr)
        {
            throw std::invalid_argument("State is null");
        }

        switch (preservationBackendKindForSettings(settings))
        {
            case PreservationBackendKind::WavPcm16:
                cupuacu::file::wav::WavPreservationWriter::writePreservingWavFile(
                    state, referencePath, outputPath, settings);
                return;
            case PreservationBackendKind::None:
            default:
                throw std::runtime_error(
                    "Selected target format does not have a preservation writer yet");
        }
    }
} // namespace cupuacu::file
