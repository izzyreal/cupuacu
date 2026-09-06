#pragma once

#include "../Undoable.hpp"
#include "RevisionEdit.hpp"
#include "../../concurrency/DeferredRelease.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>

namespace cupuacu::actions::audio
{
    class SetSampleValue : public Undoable
    {
        friend class persistence::RevisionPersistence;

    private:
        const uint32_t channel;
        const int64_t sampleIndex;
        const float oldValue;

        float newValue = oldValue;
        std::optional<RevisionEditState> revisionBefore;
        std::shared_ptr<const storage::AudioEditRevision> revisionAfter,
            appliedRevision;
        uint64_t tabId = 0;
        bool changedValue = true, committed = true;

        void captureRevision()
        {
            auto &session = state->getActiveDocumentSession();
            if (session.hasReadRevision())
            {
                revisionBefore = RevisionEditState::capture(session);
                appliedRevision = revisionBefore->audio;
                tabId = state->getActiveTab()->id;
            }
        }
        void applyRevision(bool redo)
        {
            committed = false;
            if (!state->getActiveTab() || state->getActiveTab()->id != tabId)
            {
                return;
            }
            auto &session = state->getActiveDocumentSession();
            if (session.getEditRevision() != appliedRevision)
            {
                return;
            }
            if (redo && changedValue)
            {
                storage::AudioEditTransaction edit(*revisionBefore->audio);
                edit.replaceChannel(channel, sampleIndex, 1, nullptr, 0, 0,
                                    newValue);
                revisionAfter = concurrency::releaseOnWorker(edit.finish());
                changedValue = false;
            }
            const auto &next = redo ? revisionAfter : revisionBefore->audio;
            if (session.commitEditRevision(appliedRevision, next,
                                           session.document.getMarkers()))
            {
                appliedRevision = next;
                committed = true;
            }
        }

    public:
        explicit SetSampleValue(cupuacu::State *state,
                                const uint32_t channelToUse,
                                const int64_t sampleIndexToUse,
                                const float oldValueToUse)
            : Undoable(state), channel(channelToUse),
              sampleIndex(sampleIndexToUse), oldValue(oldValueToUse)
        {
            captureRevision();
        }

        explicit SetSampleValue(cupuacu::State *state,
                                const uint32_t channelToUse,
                                const int64_t sampleIndexToUse,
                                const float oldValueToUse,
                                const float newValueToUse)
            : Undoable(state), channel(channelToUse),
              sampleIndex(sampleIndexToUse), oldValue(oldValueToUse),
              newValue(newValueToUse)
        {
            captureRevision();
        }

        void setNewValue(const float newValueToUse)
        {
            changedValue = changedValue || newValue != newValueToUse;
            newValue = newValueToUse;
        }

        bool lastOperationCommitted() const override
        {
            return committed;
        }

        void redo() override
        {
            if (revisionBefore)
            {
                applyRevision(true);
                return;
            }
            auto &session = state->getActiveDocumentSession();
            session.document.setSample(channel, sampleIndex, newValue);
            session.getWaveformCache(channel).invalidateSample(sampleIndex);
            session.updateWaveformCache();
        }

        void undo() override
        {
            if (revisionBefore)
            {
                applyRevision(false);
                return;
            }
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
            return !revisionBefore;
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (revisionBefore)
            {
                return std::nullopt;
            }
            return nlohmann::json{
                {"kind", "set-sample-value"}, {"channel", channel},
                {"sampleIndex", sampleIndex}, {"oldValue", oldValue},
                {"newValue", newValue},
            };
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::
                compatible();
        }
    };
} // namespace cupuacu::actions::audio
