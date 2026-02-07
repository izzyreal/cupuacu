#pragma once

#include "../State.hpp"
#include "../file/WavWriter.hpp"

namespace cupuacu::actions
{
    static void overwrite(cupuacu::State *state)
    {
        file::WavWriter::rewriteWavFile(state);
    }
} // namespace cupuacu::actions
