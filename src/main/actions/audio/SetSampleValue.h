#pragma once

#include "../Undoable.h"

#include <cstdint>

namespace cupuacu::actions::audio
{
    class SetSampleValue : public Undoable
    {
    private:
        const uint32_t channel;
        const int64_t sampleIndex;
        const float oldValue;

        float newValue;

    public:
        explicit SetSampleValue(cupuacu::State *state,
                                const uint32_t channelToUse,
                                const int64_t sampleIndexToUse,
                                const float oldValueToUse)
            : Undoable(state), channel(channelToUse),
              sampleIndex(sampleIndexToUse), oldValue(oldValueToUse)
        {
        }

        void setNewValue(const float newValueToUse)
        {
            newValue = newValueToUse;
        }

        void redo() override
        {
            state->document.setSample(channel, sampleIndex, newValue);
            state->document.getWaveformCache(channel).invalidateSample(
                sampleIndex);
            state->document.updateWaveformCache();
        }

        void undo() override
        {
            state->document.setSample(channel, sampleIndex, oldValue);
            state->document.getWaveformCache(channel).invalidateSample(
                sampleIndex);
            state->document.updateWaveformCache();
        }

        std::string getUndoDescription() override
        {
            return "Change sample value";
        }

        std::string getRedoDescription() override
        {
            return getUndoDescription();
        }
    };
} // namespace cupuacu::actions::audio
