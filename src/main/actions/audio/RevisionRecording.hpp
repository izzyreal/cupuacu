#pragma once
#include "RevisionEdit.hpp"
#include "../../storage/RecordingWriter.hpp"

namespace cupuacu::actions
{
    struct RevisionRecording
    {
        uint64_t tabId;
        int64_t startFrame;
        audio::RevisionEditState before;
        std::shared_ptr<const storage::AudioEditRevision> published;
        storage::RecordingWriter writer;
        bool discardRemainingInput = false;
        bool finishing = false;
        RevisionRecording(State *, int64_t start, std::filesystem::path);
    };
    void startRevisionRecording(State *, int64_t start, std::filesystem::path);
    // Call on the UI thread. Publication is metadata-only and never joins.
    bool pollRevisionRecording(State *);
    bool consumeRevisionRecordedAudio(State *);
} // namespace cupuacu::actions
