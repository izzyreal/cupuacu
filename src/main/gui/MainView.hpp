#pragma once

#include "audio/AudioDevices.hpp"
#include "Component.hpp"

#include <vector>

namespace cupuacu
{
    enum class SampleFormat;
}

namespace cupuacu::gui
{
    class Waveforms;
    class TriangleMarker;
    class OpaqueRect;
    class Timeline;

    class MainView : public Component
    {
    public:
        MainView(State *);

        void rebuildWaveforms() const;
        void resized() override;
        void timerCallback() override;
        void updateTriangleMarkerBounds() const;

    private:
        struct RecordingUndoCapture
        {
            bool active = false;
            int64_t startFrame = 0;
            int64_t endFrame = 0;
            int64_t oldFrameCount = 0;
            int oldChannelCount = 0;
            int targetChannelCount = 0;
            int oldSampleRate = 0;
            int newSampleRate = 0;
            int oldFormat = 0;
            int newFormat = 0;

            bool hadOldSelection = false;
            double oldSelectionStart = 0.0;
            double oldSelectionEnd = 0.0;
            int64_t oldCursor = 0;

            int64_t capturedOverwriteFrames = 0;
            std::vector<std::vector<float>> overwrittenOldSamples;
            std::vector<std::vector<float>> recordedSamples;
        };
        bool consumePendingRecordedAudio();
        void beginRecordingUndoCaptureIfNeeded(int64_t startFrame);
        void capturePreOverwriteSamples(int64_t overlapEndFrame);
        void captureRecordedChunk(
            const cupuacu::audio::AudioDevices::RecordedChunk &chunk);
        void finalizeRecordingUndoCaptureIfComplete();
        bool followTransportHead();
        TriangleMarker *cursorTop;
        TriangleMarker *cursorBottom;
        TriangleMarker *selStartTop;
        TriangleMarker *selStartBot;
        TriangleMarker *selEndTop;
        TriangleMarker *selEndBot;
        int64_t lastDrawnCursor = -1;
        bool lastSelectionIsActive = true;
        int64_t lastSampleOffset = -1;
        double lastSamplesPerPixel = 0.0;
        int64_t lastSelectionStart = -1;
        int64_t lastSelectionEnd = -1;
        bool wasRecordingLastTick = false;
        RecordingUndoCapture recordingUndoCapture;

        const uint8_t baseBorderWidth = 16;
        uint8_t computeBorderWidth() const;
        void resizeWaveforms();

        Waveforms *waveforms = nullptr;
        OpaqueRect *borders[4];
        Timeline *timeline = nullptr;
    };
} // namespace cupuacu::gui
