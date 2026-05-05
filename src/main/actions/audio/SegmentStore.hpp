#pragma once

#include "../../DocumentSession.hpp"

namespace cupuacu::actions::audio::detail
{
    inline undo::UndoStore::SegmentHandle storeSegmentIfNeeded(
        cupuacu::DocumentSession &session, undo::UndoStore::SegmentHandle handle,
        const cupuacu::Document::AudioSegment &segment, const char *prefix)
    {
        if (handle.empty())
        {
            return session.undoStore.writeSegment(segment, prefix);
        }
        return handle;
    }

    template <typename CaptureFn>
    inline cupuacu::Document::AudioSegment captureOrLoadSegment(
        cupuacu::DocumentSession &session, undo::UndoStore::SegmentHandle handle,
        CaptureFn &&capture)
    {
        return handle.empty() ? capture() : session.undoStore.readSegment(handle);
    }
} // namespace cupuacu::actions::audio::detail
