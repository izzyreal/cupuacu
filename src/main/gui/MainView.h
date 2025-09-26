#pragma once

#include "Component.h"

class MainView : public Component {
    public:
        MainView(CupuacuState*);

        void rebuildWaveforms();

        void resized() override;

    private:
        void resizeWaveforms();
        Component *waveformsUnderlay = nullptr;
};
