#pragma once

#include "gui/Selection.h"

#include <cstdint>
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
    uint8_t hardwarePixelsPerAppPixel = 1;
    std::string currentFile = "/Users/izmar/Downloads/ams_chill.wav";

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
    double sampleOffset;
    Selection<double> selection = Selection<double>(0.0);
    int selectionChannelStart = -1;
    int selectionChannelEnd   = -1;
    int selectionAnchorChannel;
    double samplesToScroll;

    std::atomic<double> playbackPosition = 0;
    std::atomic<bool> isPlaying = false;

    std::shared_ptr<CustomDataSource> activePlayback;

    SDL_Window *window = nullptr;

    Component *capturingComponent = nullptr;
    Component *componentUnderMouse = nullptr;

    std::vector<Waveform*> waveforms;

    std::function<void()> hideSubMenus = []{};

};

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

