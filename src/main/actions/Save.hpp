#pragma once

#include "../State.hpp"
#include "../file/WavWriter.hpp"
#include "DocumentLifecycle.hpp"

namespace cupuacu::actions
{
    static void overwrite(cupuacu::State *state)
    {
        file::WavWriter::rewriteWavFile(state);
    }

    static void saveAs(cupuacu::State *state, const std::string &absoluteFilePath)
    {
        if (!state || absoluteFilePath.empty())
        {
            return;
        }

        file::WavWriter::writeWavFile(state, absoluteFilePath);
        state->getActiveDocumentSession().currentFile = absoluteFilePath;
        rememberRecentFile(state, absoluteFilePath);
        setMainWindowTitle(state, absoluteFilePath);
    }
} // namespace cupuacu::actions
