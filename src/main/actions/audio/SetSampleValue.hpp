#pragma once

#include "../Undoable.hpp"

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
            auto &session = state->getActiveDocumentSession();
            session.document.setSample(channel, sampleIndex, newValue);
            session.getWaveformCache(channel).invalidateSample(sampleIndex);
            session.updateWaveformCache();
        }

        void undo() override
        {
            auto &session = state->getActiveDocumentSession();
            session.document.setSample(channel, sampleIndex, oldValue);
            session.getWaveformCache(channel).invalidateSample(sampleIndex);
            session.updateWaveformCache();
        }

        std::string getUndoDescription() override
        {
            return "Change sample value";
        }

        std::string getRedoDescription() override
        {
            return getUndoDescription();
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return false;
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }
    };
} // namespace cupuacu::actions::audio
