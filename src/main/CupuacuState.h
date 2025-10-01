#pragma once

#include <SDL3/SDL.h>

#include "gui/Selection.h"

#include <cstdint>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <optional>

struct CustomDataSource;

class MenuBar;
class Component;
class Waveform;
class MainView;

enum class SampleFormat {
    PCM_S8, PCM_S16, PCM_S24, PCM_S32,
    FLOAT32, FLOAT64,
    Unknown
};

enum SelectedChannels {
    BOTH, LEFT, RIGHT
};
 
struct CupuacuState {
    uint8_t menuFontSize = 40;
    uint8_t pixelScale = 1;
    std::string currentFile = "/Users/izmar/Downloads/ams_chill.wav";

    std::vector<SDL_Rect> dirtyRects;

    struct Document {
        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;
        std::vector<std::vector<float>> channels;

        int64_t getFrameCount() const
        {
            if (channels.empty())
            {
                return 0;
            }

            return channels[0].size();
        }
    } document;

    double samplesPerPixel = 1;
    double verticalZoom;
    int64_t sampleOffset;
    Selection<double> selection = Selection<double>(0.0);
    SelectedChannels selectedChannels;
    SelectedChannels hoveringOverChannels;
    double samplesToScroll;
    std::optional<float> sampleValueUnderMouseCursor;

    int64_t cursor = 0;
    std::atomic<int64_t> playbackPosition;
    std::atomic<bool> isPlaying = false;

    std::shared_ptr<CustomDataSource> activePlayback;

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *canvas = NULL;
    SDL_Texture *textTexture = NULL;

    Component *capturingComponent = nullptr;
    Component *componentUnderMouse = nullptr;

    std::vector<Waveform*> waveforms;
    std::unique_ptr<Component> rootComponent;
    MenuBar *menuBar;
    Component *statusBar;
    MainView *mainView;
};

static void resetSampleValueUnderMouseCursor(CupuacuState *state)
{
    state->sampleValueUnderMouseCursor.reset();
}

static void updateSampleValueUnderMouseCursor(CupuacuState *state, const float sampleValue)
{
    state->sampleValueUnderMouseCursor.emplace(sampleValue);
}

static void resetWaveformState(CupuacuState *state)
{
    state->verticalZoom = 1;
    state->sampleOffset = 0;
    state->selection.reset();
    state->selectedChannels = SelectedChannels::BOTH;
    state->samplesToScroll = 0;
    state->playbackPosition.store(-1);
}

int64_t getMaxSampleOffset(const CupuacuState*);

static void updateSampleOffset(CupuacuState *state, const int64_t sampleOffset)
{
    state->sampleOffset = std::clamp(sampleOffset, int64_t{0}, getMaxSampleOffset(state));
}

static bool updateCursorPos(CupuacuState *state, const int64_t cursorPos)
{
    const int64_t oldCursor = state->cursor;
    state->cursor = std::clamp(cursorPos, int64_t{0}, state->document.getFrameCount());
    return state->cursor != oldCursor;
}

