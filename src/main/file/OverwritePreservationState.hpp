#pragma once

#include <string>

namespace cupuacu::file
{
    struct OverwritePreservationState
    {
        bool available = false;
        std::string reason;
    };
} // namespace cupuacu::file
