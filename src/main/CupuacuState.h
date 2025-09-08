#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <memory>

struct Component;
struct SDL_Window;
struct CustomDataSource;

struct CupuacuState {
    uint8_t menuFontSize = 60;
    uint8_t hardwarePixelsPerAppPixel = 1;
    std::string currentFile = "/Users/izmar/Downloads/ams_chill.wav";

    std::vector<int16_t> sampleDataL;
    std::vector<int16_t> sampleDataR;

    double samplesPerPixel = 1;
    double verticalZoom;
    double sampleOffset;
    double selectionStartSample;
    double selectionEndSample;
    double samplesToScroll;

    std::atomic<double> playbackPosition = 0;
    std::atomic<bool> isPlaying = false;

    std::shared_ptr<CustomDataSource> activePlayback;  // <-- keeps playback alive

    SDL_Window *window = nullptr;

    Component *capturingComponent = nullptr;
    Component *componentUnderMouse = nullptr;

    Component *waveformComponent = nullptr;

    std::function<void()> hideSubMenus = []{};
};

static void resetWaveformState(CupuacuState *state)
{
    state->verticalZoom = 1;
    state->sampleOffset = 0;
    state->selectionStartSample = 0;
    state->selectionEndSample = 0;
    state->samplesToScroll = 0;
    state->playbackPosition = 0;
}

