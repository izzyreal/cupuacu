#pragma once

#include "ControlPointHandle.hpp"
#include "../State.hpp"

namespace cupuacu::actions::audio
{
    class SetSampleValue;
}

namespace cupuacu::gui
{

    class SamplePoint : public ControlPointHandle
    {
    private:
        const int64_t sampleIndex;
        const uint8_t channelIndex;
        bool isDragging = false;
        float dragYPos = 0.f;
        std::optional<float> displayedValue;
        std::shared_ptr<const storage::AudioEditRevision> displayedRevision;

        std::shared_ptr<actions::audio::SetSampleValue> undoable;

    public:
        SamplePoint(State *, const uint8_t channelIndexToUse,
                    const int64_t sampleIndexToUse,
                    std::optional<float> value = {});

        bool isBeingDragged() const
        {
            return isDragging;
        }
        uint64_t getSampleIndex() const;
        float getSampleValue() const;

        bool mouseDown(const MouseEvent &) override;
        bool mouseUp(const MouseEvent &) override;
        bool mouseMove(const MouseEvent &) override;
    };
} // namespace cupuacu::gui
