#pragma once

#include "Component.h"

namespace cupuacu::gui {

class Waveforms : public Component {
    public:
        Waveforms(cupuacu::State*);

        void rebuildWaveforms();

        void resizeWaveforms();

        void resized() override;

    private:
        Component *waveformsUnderlay = nullptr;
        uint32_t previousWidth = 0;
};
}
