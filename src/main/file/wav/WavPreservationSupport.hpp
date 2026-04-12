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
        assessOverwrite(const cupuacu::State *state)
        {
            if (state == nullptr)
            {
                return {.supported = false, .reason = "State is null"};
            }

            const auto &session = state->getActiveDocumentSession();
            if (session.currentFile.empty())
            {
                return {.supported = false, .reason = "No current file"};
            }

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

            if (!cupuacu::file::isOverwritePreservingWavRewriteCandidate(
                    *settings))
            {
                return {.supported = false,
                        .reason = "Current file is not a WAV PCM16 preservation candidate"};
            }

            if (session.document.getSampleFormat() != cupuacu::SampleFormat::PCM_S16)
            {
                return {.supported = false,
                        .reason = "Document is not 16-bit PCM"};
            }

            ParsedFile parsed{};
            try
            {
                parsed = WavParser::parseFile(session.currentFile);
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
    };
} // namespace cupuacu::file::wav
