#pragma once

#include "../../State.hpp"
#include "../AudioExport.hpp"
#include "AiffParser.hpp"

#include <sndfile.h>

#include <string>

namespace cupuacu::file::aiff
{
    struct OverwritePreservationSupport
    {
        bool supported = false;
        std::string reason;
    };

    class AiffPreservationSupport
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

            if (!(settings.majorFormat == SF_FORMAT_AIFF &&
                  settings.subtype == SF_FORMAT_PCM_16))
            {
                return {.supported = false,
                        .reason =
                            "Selected target format is not an AIFF PCM16 preservation candidate"};
            }

            if (session.document.getSampleFormat() != cupuacu::SampleFormat::PCM_S16)
            {
                return {.supported = false,
                        .reason = "Document is not 16-bit PCM"};
            }

            ParsedFile parsed{};
            try
            {
                parsed = AiffParser::parseFile(referenceFile);
            }
            catch (const std::exception &e)
            {
                return {.supported = false, .reason = e.what()};
            }

            if (!parsed.isPcm16)
            {
                return {.supported = false,
                        .reason = "Not a 16-bit PCM AIFF file"};
            }
            if (parsed.commChunkCount != 1)
            {
                return {.supported = false,
                        .reason =
                            "Unsupported AIFF structure: expected exactly one COMM chunk"};
            }
            if (parsed.ssndChunkCount != 1)
            {
                return {.supported = false,
                        .reason =
                            "Unsupported AIFF structure: expected exactly one SSND chunk"};
            }
            if (parsed.findChunk("COMM") == nullptr)
            {
                return {.supported = false, .reason = "COMM chunk not found"};
            }
            if (parsed.findChunk("SSND") == nullptr)
            {
                return {.supported = false, .reason = "SSND chunk not found"};
            }
            if (session.document.getChannelCount() != parsed.channelCount)
            {
                return {.supported = false,
                        .reason =
                            "Document channel count does not match source AIFF"};
            }
            if (session.document.getSampleRate() != parsed.sampleRate)
            {
                return {.supported = false,
                        .reason =
                            "Document sample rate does not match source AIFF"};
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
} // namespace cupuacu::file::aiff
