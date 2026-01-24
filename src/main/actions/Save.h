#pragma once

#include "../State.h"
#include "../file/WavWriter.h"

namespace cupuacu::actions
{
    static void overwrite(cupuacu::State *state)
    {
        file::WavWriter::rewriteWavFile(state);
    }
} // namespace cupuacu::actions
