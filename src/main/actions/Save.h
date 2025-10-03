#pragma once

#include "../CupuacuState.h"
#include "../file/WavWriter.h"

static void overwrite(CupuacuState *state)
{
    WavWriter::rewriteWavFile(state);
}

