#pragma once

#include <SDL3/SDL.h>

#include "gui/Selection.h"

#include <cstdint>
#include <limits>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <memory>

struct SDL_Window;

struct CustomDataSource;

class Component;
class Waveform;

enum class SampleFormat {
    PCM_S8, PCM_S16, PCM_S24, PCM_S32,
    FLOAT32, FLOAT64,
    Unknown
};
 
struct CupuacuState {
    uint8_t menuFontSize = 60;
    uint8_t pixelScale = 4;
    std::string currentFile = "/Users/izmar/Downloads/env.wav";

    struct Document {
        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;
        std::vector<std::vector<float>> channels;
        size_t getFrameCount()
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
    size_t sampleOffset;
    Selection<double> selection = Selection<double>(0.0);
    int selectionChannelStart = -1;
    int selectionChannelEnd   = -1;
    int selectionAnchorChannel;
    double samplesToScroll;
    float sampleValueUnderMouseCursor;

    std::atomic<double> playbackPosition = 0;
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
    Component *backgroundComponentHandle;
    Component *menuBarHandle;
    Component *statusBarHandle;
    Component *waveformsOverlayHandle;

    std::function<void()> hideSubMenus = []{};

};

static void resetSampleValueUnderMouseCursor(CupuacuState *state)
{
    state->sampleValueUnderMouseCursor = std::numeric_limits<float>::lowest(); 
}

static void resetWaveformState(CupuacuState *state)
{
    state->verticalZoom = 1;
    state->sampleOffset = 0;
    state->selection.reset();
    state->selectionChannelStart = -1;
    state->selectionChannelEnd = -1;
    state->samplesToScroll = 0;
    state->playbackPosition = 0;
}

size_t getMaxSampleOffset(CupuacuState *state);

static void updateSampleOffset(CupuacuState *state, const size_t sampleOffsetToUse)
{
    printf("Current offset: %zu\n", state->sampleOffset);
    printf("Trying to set offset to %zu\n", sampleOffsetToUse);
    state->sampleOffset = std::min(getMaxSampleOffset(state), sampleOffsetToUse);
    printf("New sample offset: %zu\n", state->sampleOffset);
}

