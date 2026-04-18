#pragma once

#include "../../State.hpp"
#include "../AudioExport.hpp"
#include "WavParser.hpp"

#include <string>

namespace cupuacu::file::wav
{
    struct OverwritePreservationSupport
    {
        bool supported = false;
        std::string reason;
    };

    class WavPreservationSupport
    {
    public:
        [[nodiscard]] static OverwritePreservationSupport
        assessAgainstReference(const cupuacu::State *state,
                               const cupuacu::file::AudioExportSettings &settings)
        {
            if (state == nullptr)
            {
                return {.supported = false, .reason = "State is null"};
            }

            const auto &session = state->getActiveDocumentSession();
            const auto referenceFile = !session.preservationReferenceFile.empty()
                                           ? session.preservationReferenceFile
                                           : session.currentFile;
            if (referenceFile.empty())
            {
                return {.supported = false, .reason = "No preservation reference"};
            }

            if (!cupuacu::file::isOverwritePreservingWavRewriteCandidate(settings))
            {
                return {.supported = false,
                        .reason =
                            "Selected target format is not a WAV PCM16 preservation candidate"};
            }

            if (session.document.getSampleFormat() != cupuacu::SampleFormat::PCM_S16)
            {
                return {.supported = false,
                        .reason = "Document is not 16-bit PCM"};
            }

            ParsedFile parsed{};
            try
            {
                parsed = WavParser::parseFile(referenceFile);
            }
            catch (const std::exception &e)
            {
                return {.supported = false, .reason = e.what()};
            }

            if (!parsed.isPcm16)
            {
                return {.supported = false, .reason = "Not a 16-bit PCM WAV file"};
            }
            if (parsed.fmtChunkCount != 1)
            {
                return {.supported = false,
                        .reason =
                            "Unsupported WAV structure: expected exactly one fmt chunk"};
            }
            if (parsed.dataChunkCount != 1)
            {
                return {.supported = false,
                        .reason =
                            "Unsupported WAV structure: expected exactly one data chunk"};
            }
            if (parsed.findChunk("data") == nullptr)
            {
                return {.supported = false, .reason = "data chunk not found"};
            }
            if (session.document.getChannelCount() != parsed.channelCount)
            {
                return {.supported = false,
                        .reason =
                            "Document channel count does not match source WAV"};
            }
            if (session.document.getSampleRate() != parsed.sampleRate)
            {
                return {.supported = false,
                        .reason =
                            "Document sample rate does not match source WAV"};
            }

            return {.supported = true};
        }

        [[nodiscard]] static OverwritePreservationSupport
        assessOverwrite(const cupuacu::State *state)
        {
            if (state == nullptr)
            {
                return {.supported = false, .reason = "State is null"};
            }

            const auto &session = state->getActiveDocumentSession();
            auto settings = session.currentFileExportSettings;
            if (!settings.has_value())
            {
                settings = cupuacu::file::defaultExportSettingsForPath(
                    session.currentFile, session.document.getSampleFormat());
            }
            if (!settings.has_value())
            {
                return {.supported = false,
                        .reason = "Could not determine file export settings"};
            }

            return assessAgainstReference(state, *settings);
        }
    };
} // namespace cupuacu::file::wav
