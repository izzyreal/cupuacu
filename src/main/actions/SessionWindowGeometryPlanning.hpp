#pragma once

#include "../persistence/SessionStatePersistence.hpp"

#include <optional>

namespace cupuacu::actions
{
    inline void applyPersistedWindowGeometry(
        cupuacu::persistence::PersistedSessionState &persisted,
        const std::optional<int> width, const std::optional<int> height,
        const std::optional<int> x, const std::optional<int> y)
    {
        if (!width.has_value() || !height.has_value() || *width <= 0 ||
            *height <= 0)
        {
            return;
        }

        persisted.windowWidth = *width;
        persisted.windowHeight = *height;
        if (x.has_value() && y.has_value())
        {
            persisted.windowX = *x;
            persisted.windowY = *y;
        }
    }
} // namespace cupuacu::actions
