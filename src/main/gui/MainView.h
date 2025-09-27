#pragma once

#include "Component.h"

class Waveforms;

class MainView : public Component {
    public:
        MainView(CupuacuState*);

        void rebuildWaveforms();

        void resized() override;

        void onDraw(SDL_Renderer*) override;

        void timerCallback() override;

    private:
        int64_t lastDrawnCursor = -1;
        bool lastSelectionIsActive = true;
        const uint8_t baseBorderWidth = 16;
        uint8_t computeBorderWidth() const;
        void resizeWaveforms();
        Waveforms *waveforms = nullptr;
        void drawCursorTriangles(SDL_Renderer *r);
};
