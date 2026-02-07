#pragma once

#include "Component.hpp"

namespace cupuacu::gui
{

    class Waveforms : public Component
    {
    public:
        Waveforms(State *);

        void rebuildWaveforms();

        void resizeWaveforms() const;

        void resized() override;

    private:
        Component *waveformsUnderlay = nullptr;
        uint32_t previousWidth = 0;
    };
} // namespace cupuacu::gui
