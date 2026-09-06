#pragma once

#include "AudioExport.hpp"
#include "OverwritePreservationState.hpp"
#include "../DocumentSession.hpp"
#include "../storage/AudioEditRevision.hpp"
#include "AudioFileWriter.hpp"

namespace cupuacu::file
{
    std::filesystem::path
    revisionPreservationReference(const DocumentSession &);
    OverwritePreservationState
    assessRevisionPreservation(const DocumentSession &,
                               const AudioExportSettings &);

    // Worker-only, transactional rewrite. Untouched PCM is copied from retained
    // source files, including precision that decoded float samples cannot hold.
    void writePreservingRevision(const storage::AudioEditRevision &,
                                 const std::vector<DocumentMarker> &,
                                 const std::filesystem::path &reference,
                                 const std::filesystem::path &output,
                                 const AudioExportSettings &,
                                 const WriteProgressCallback &progress = {});
} // namespace cupuacu::file
