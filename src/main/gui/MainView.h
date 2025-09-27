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
        int64_t lastSampleOffset = -1;
        double lastSamplesPerPixel = 0.0;
        int64_t lastSelectionStart = -1;
        int64_t lastSelectionEnd = -1;

        const uint8_t baseBorderWidth = 16;
        uint8_t computeBorderWidth() const;
        void resizeWaveforms();
        Waveforms *waveforms = nullptr;
        void drawTriangle(SDL_Renderer *r,
                          const SDL_FPoint (&pts)[3],
                          const SDL_FColor &color);
        void drawCursorTriangles(SDL_Renderer*);
        void drawSelectionTriangles(SDL_Renderer*);
};
