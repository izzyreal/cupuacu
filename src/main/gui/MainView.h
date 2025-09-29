#pragma once

#include "Component.h"

class Waveforms;
class TriangleMarker;

class MainView : public Component {
    public:
        MainView(CupuacuState*);

        void rebuildWaveforms();

        void resized() override;

        void onDraw(SDL_Renderer*) override;

        void timerCallback() override;

        void updateTriangleMarkerBounds();

    private:
        TriangleMarker *cursorTop = nullptr;
        TriangleMarker *cursorBottom = nullptr;
        TriangleMarker *selStartTop = nullptr;
        TriangleMarker *selStartBot = nullptr;
        TriangleMarker *selEndTop = nullptr;
        TriangleMarker *selEndBot = nullptr;
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

};
