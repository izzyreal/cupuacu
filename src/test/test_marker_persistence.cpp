#include <catch2/catch_test_macros.hpp>

#include "Document.hpp"
#include "file/MarkerPersistence.hpp"

#include <sndfile.h>

TEST_CASE("WAV marker persistence contract reports exact native support",
          "[file][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, "Kick");
    document.addMarker(8, "Snare");

    const cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };

    const auto assessment =
        cupuacu::file::assessMarkerPersistenceForSettings(document, settings);

    REQUIRE(assessment.capabilities.kind ==
            cupuacu::file::MarkerNativePersistenceKind::WavCueAdtl);
    REQUIRE(assessment.capabilities.preservesFramePosition);
    REQUIRE(assessment.capabilities.preservesLabel);
    REQUIRE_FALSE(assessment.capabilities.maxLabelBytes.has_value());
    REQUIRE(assessment.fidelity ==
            cupuacu::file::MarkerPersistenceFidelity::Exact);
    REQUIRE_FALSE(assessment.requiresSidecarOrSessionFallback);
}

TEST_CASE("AIFF marker persistence contract reports lossy support for long labels",
          "[file][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, std::string(300, 'x'));

    const cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::AIFF,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_AIFF,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "AIFF",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "aiff",
    };

    const auto assessment =
        cupuacu::file::assessMarkerPersistenceForSettings(document, settings);

    REQUIRE(assessment.capabilities.kind ==
            cupuacu::file::MarkerNativePersistenceKind::AiffMark);
    REQUIRE(assessment.capabilities.maxLabelBytes.has_value());
    REQUIRE(*assessment.capabilities.maxLabelBytes == 255);
    REQUIRE(assessment.fidelity ==
            cupuacu::file::MarkerPersistenceFidelity::Lossy);
    REQUIRE(assessment.requiresSidecarOrSessionFallback);
}

TEST_CASE("AIFF marker persistence contract reports exact support for short labels",
          "[file][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, "Short");

    const cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::AIFF,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_AIFF,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "AIFF",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "aiff",
    };

    const auto assessment =
        cupuacu::file::assessMarkerPersistenceForSettings(document, settings);

    REQUIRE(assessment.fidelity ==
            cupuacu::file::MarkerPersistenceFidelity::Exact);
    REQUIRE_FALSE(assessment.requiresSidecarOrSessionFallback);
}

TEST_CASE("Formats without native marker support require fallback when markers exist",
          "[file][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, "Kick");

    const cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::FLAC,
        .codec = cupuacu::file::AudioExportCodec::FLAC,
        .majorFormat = SF_FORMAT_FLAC,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "FLAC",
        .codecLabel = "FLAC",
        .encodingLabel = "16-bit FLAC",
        .extension = "flac",
    };

    const auto assessment =
        cupuacu::file::assessMarkerPersistenceForSettings(document, settings);

    REQUIRE(assessment.capabilities.kind ==
            cupuacu::file::MarkerNativePersistenceKind::None);
    REQUIRE(assessment.fidelity ==
            cupuacu::file::MarkerPersistenceFidelity::Unsupported);
    REQUIRE(assessment.requiresSidecarOrSessionFallback);
}

TEST_CASE("Formats without native marker support do not require fallback when there are no markers",
          "[file][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);

    const cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::FLAC,
        .codec = cupuacu::file::AudioExportCodec::FLAC,
        .majorFormat = SF_FORMAT_FLAC,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "FLAC",
        .codecLabel = "FLAC",
        .encodingLabel = "16-bit FLAC",
        .extension = "flac",
    };

    const auto assessment =
        cupuacu::file::assessMarkerPersistenceForSettings(document, settings);

    REQUIRE(assessment.fidelity ==
            cupuacu::file::MarkerPersistenceFidelity::Unsupported);
    REQUIRE_FALSE(assessment.requiresSidecarOrSessionFallback);
}
