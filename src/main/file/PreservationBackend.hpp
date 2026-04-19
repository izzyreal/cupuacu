#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "OverwritePreservationState.hpp"
#include "aiff/AiffPreservationSupport.hpp"
#include "aiff/AiffPreservationWriter.hpp"
#include "wav/WavPreservationSupport.hpp"
#include "wav/WavPreservationWriter.hpp"

#include <sndfile.h>

#include <filesystem>
#include <stdexcept>

namespace cupuacu::file
{
    enum class PreservationBackendKind
    {
        None,
        AiffPcm16,
        WavPcm16,
    };

    inline PreservationBackendKind
    preservationBackendKindForSettings(const AudioExportSettings &settings)
    {
        if (isOverwritePreservingWavRewriteCandidate(settings))
        {
            return PreservationBackendKind::WavPcm16;
        }
        if (settings.majorFormat == SF_FORMAT_AIFF &&
            settings.subtype == SF_FORMAT_PCM_16)
        {
            return PreservationBackendKind::AiffPcm16;
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
            case PreservationBackendKind::AiffPcm16:
            {
                const auto support =
                    cupuacu::file::aiff::AiffPreservationSupport::
                        assessAgainstReference(state, settings);
                return {.available = support.supported, .reason = support.reason};
            }
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
            case PreservationBackendKind::AiffPcm16:
            {
                const auto support =
                    cupuacu::file::aiff::AiffPreservationSupport::assessOverwrite(
                        state);
                return {.available = support.supported, .reason = support.reason};
            }
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
            case PreservationBackendKind::AiffPcm16:
                cupuacu::file::aiff::AiffPreservationWriter::
                    writePreservingAiffFile(state, referencePath, outputPath,
                                            settings);
                return;
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

    inline void overwritePreservingCurrentFile(cupuacu::State *state,
                                               const AudioExportSettings &settings)
    {
        if (state == nullptr)
        {
            throw std::invalid_argument("State is null");
        }

        switch (preservationBackendKindForSettings(settings))
        {
            case PreservationBackendKind::AiffPcm16:
                cupuacu::file::aiff::AiffPreservationWriter::
                    overwritePreservingAiffFile(state);
                return;
            case PreservationBackendKind::WavPcm16:
                cupuacu::file::wav::WavPreservationWriter::
                    overwritePreservingWavFile(state);
                return;
            case PreservationBackendKind::None:
            default:
                throw std::runtime_error(
                    "Current file format does not have a preservation writer yet");
        }
    }
} // namespace cupuacu::file
