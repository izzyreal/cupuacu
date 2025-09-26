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
 
struct CupuacuState {
    uint8_t menuFontSize = 60;
    uint8_t pixelScale = 4;
    std::string currentFile = "/Users/izmar/Downloads/ams_chill.wav";

    struct Document {
        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;
        std::vector<std::vector<float>> channels;

        size_t getFrameCount() const
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
    std::optional<uint8_t> selectionChannelStart;
    std::optional<uint8_t> selectionChannelEnd;
    std::optional<uint8_t> selectionAnchorChannel;
    double samplesToScroll;
    std::optional<float> sampleValueUnderMouseCursor;

    std::atomic<size_t> playbackPosition = 0;
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
    Component *backgroundComponent;
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
    state->selectionChannelStart = -1;
    state->selectionChannelEnd = -1;
    state->samplesToScroll = 0;
    state->playbackPosition = 0;
}

size_t getMaxSampleOffset(const CupuacuState*);

static void updateSampleOffset(CupuacuState *state, const size_t sampleOffset)
{
    state->sampleOffset = std::min(getMaxSampleOffset(state), sampleOffset);
}

