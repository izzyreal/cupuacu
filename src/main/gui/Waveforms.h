#pragma once

#include "Component.h"

class Waveforms : public Component {
    public:
        Waveforms(CupuacuState*);

        void rebuildWaveforms();

        void resizeWaveforms();

        void resized() override;

    private:
        Component *waveformsUnderlay = nullptr;
        uint32_t previousWidth = 0;
};
