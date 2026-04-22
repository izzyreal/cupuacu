#pragma once

#include "audio/AudioDevices.hpp"
#include "Component.hpp"

#include <optional>
#include <vector>

namespace cupuacu
{
    enum class SampleFormat;
}

namespace cupuacu::gui
{
    class Waveforms;
    class TriangleMarker;
    class DocumentMarkerHandle;
    class OpaqueRect;
    class Timeline;
    class ScrollBar;

    class MainView : public Component
    {
    public:
        MainView(State *);

        void onDraw(SDL_Renderer *renderer) override;
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
        bool shouldKeepConsumingRecordedAudio(bool isRecordingNow,
                                              uint64_t chunksThisTick,
                                              uint64_t perfStart,
                                              uint64_t perfFreq) const;
        void applyRecordedChunkToSession(
            const cupuacu::audio::AudioDevices::RecordedChunk &chunk,
            bool &channelLayoutChanged, bool &waveformCacheChanged);
        void refreshWaveformsAfterRecordedAudio(bool channelLayoutChanged,
                                                bool waveformCacheChanged);
        bool followTransportHead();
        int64_t getPlaybackPositionForWaveforms() const;
        void updateWaveformPlaybackPositions() const;
        bool isSelectionInteractionActive() const;
        void syncLivePlaybackRange(bool selectionActive,
                                   SelectedChannels selectedChannels);
        void rebuildDocumentMarkerHandles() const;
        bool shouldRefreshMarkerBounds(bool consumedRecordedAudio,
                                       bool followedTransport,
                                       bool selectionActive,
                                       int64_t selectionStart,
                                       int64_t selectionEnd) const;
        void rememberMarkerInputs(bool selectionActive, int64_t selectionStart,
                                  int64_t selectionEnd);
        TriangleMarker *cursorTop;
        TriangleMarker *cursorBottom;
        TriangleMarker *selStartTop;
        TriangleMarker *selStartBot;
        TriangleMarker *selEndTop;
        TriangleMarker *selEndBot;
        mutable std::vector<DocumentMarkerHandle *> documentMarkerTopHandles;
        mutable std::vector<DocumentMarkerHandle *> documentMarkerBottomHandles;
        int64_t lastDrawnCursor = -1;
        bool lastSelectionIsActive = true;
        int64_t lastSampleOffset = -1;
        double lastSamplesPerPixel = 0.0;
        int64_t lastSelectionStart = -1;
        int64_t lastSelectionEnd = -1;
        uint64_t lastMarkerDataVersion = 0;
        std::optional<uint64_t> lastSelectedMarkerId;
        bool lastPlaybackLoopEnabled = false;
        uint64_t lastPlaybackUpdateStart = 0;
        uint64_t lastPlaybackUpdateEnd = 0;
        bool lastPlaybackUpdateSelectionActive = false;
        SelectedChannels lastPlaybackUpdateChannels = SelectedChannels::BOTH;
        bool wasRecordingLastTick = false;
        RecordingUndoCapture recordingUndoCapture;

        const uint8_t baseBorderWidth = 16;
        uint8_t computeBorderWidth() const;
        void resizeWaveforms();

        Waveforms *waveforms = nullptr;
        OpaqueRect *borders[4];
        Timeline *timeline = nullptr;
        ScrollBar *horizontalScrollBar = nullptr;
    };
} // namespace cupuacu::gui
