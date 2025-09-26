#pragma once

#include "Component.h"

class Waveforms;

class MainView : public Component {
    public:
        MainView(CupuacuState*);

        void rebuildWaveforms();

        void resized() override;

        void onDraw(SDL_Renderer*) override;

    private:
        void resizeWaveforms();
        Waveforms *waveforms = nullptr;
};
