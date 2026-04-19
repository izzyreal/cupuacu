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
    private:
        [[nodiscard]] static std::string describeFormat(
            const cupuacu::SampleFormat format)
        {
            switch (format)
            {
                case cupuacu::SampleFormat::PCM_S8:
                    return "8-bit PCM";
                case cupuacu::SampleFormat::PCM_S16:
                    return "16-bit PCM";
                case cupuacu::SampleFormat::FLOAT32:
                    return "32-bit float";
                default:
                    return "unsupported";
            }
        }

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
                  (settings.subtype == SF_FORMAT_PCM_S8 ||
                   settings.subtype == SF_FORMAT_PCM_16 ||
                   settings.subtype == SF_FORMAT_FLOAT)))
            {
                return {.supported = false,
                        .reason =
                            "Selected target format is not an AIFF preserving PCM candidate"};
            }

            const auto documentFormat = session.document.getSampleFormat();
            if (documentFormat != cupuacu::SampleFormat::PCM_S8 &&
                documentFormat != cupuacu::SampleFormat::PCM_S16 &&
                documentFormat != cupuacu::SampleFormat::FLOAT32)
            {
                return {.supported = false,
                        .reason = "Document is not a supported preserving AIFF/AIFC format"};
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

            if (parsed.sampleFormat != documentFormat ||
                parsed.sampleFormat == cupuacu::SampleFormat::Unknown)
            {
                return {.supported = false,
                        .reason = "Source AIFF format does not match document format (" +
                                  describeFormat(documentFormat) + ")"};
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
