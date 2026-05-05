#pragma once

#include "../../DocumentSession.hpp"

#include <optional>
#include <vector>

namespace cupuacu::actions::audio::detail
{
    using SampleMatrix = std::vector<std::vector<float>>;
    using SampleCube = std::vector<std::vector<std::vector<float>>>;

    inline undo::UndoStore::SampleMatrixHandle storeSampleMatrixIfNeeded(
        cupuacu::DocumentSession &session,
        undo::UndoStore::SampleMatrixHandle handle,
        const std::optional<SampleMatrix> &samples, const char *prefix)
    {
        if (handle.empty() && samples.has_value())
        {
            return session.undoStore.writeSampleMatrix(*samples, prefix);
        }
        return handle;
    }

    inline undo::UndoStore::SampleCubeHandle storeSampleCubeIfNeeded(
        cupuacu::DocumentSession &session,
        undo::UndoStore::SampleCubeHandle handle,
        const std::optional<SampleCube> &samples, const char *prefix)
    {
        if (handle.empty() && samples.has_value())
        {
            return session.undoStore.writeSampleCube(*samples, prefix);
        }
        return handle;
    }

    inline SampleMatrix materializeSampleMatrix(
        cupuacu::DocumentSession &session,
        std::optional<SampleMatrix> &pendingSamples,
        undo::UndoStore::SampleMatrixHandle &handle, const char *prefix)
    {
        if (pendingSamples.has_value())
        {
            handle = storeSampleMatrixIfNeeded(session, handle, pendingSamples,
                                               prefix);
            SampleMatrix materialized = std::move(*pendingSamples);
            pendingSamples.reset();
            return materialized;
        }

        return session.undoStore.readSampleMatrix(handle);
    }

    inline SampleCube materializeSampleCube(
        cupuacu::DocumentSession &session, std::optional<SampleCube> &pendingSamples,
        undo::UndoStore::SampleCubeHandle &handle, const char *prefix)
    {
        if (pendingSamples.has_value())
        {
            handle = storeSampleCubeIfNeeded(session, handle, pendingSamples,
                                             prefix);
            SampleCube materialized = std::move(*pendingSamples);
            pendingSamples.reset();
            return materialized;
        }

        return session.undoStore.readSampleCube(handle);
    }
} // namespace cupuacu::actions::audio::detail
