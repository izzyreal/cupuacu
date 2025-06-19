#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *canvas = NULL;

const uint16_t initialDimensions[] = { 1280, 720 };

#include <cstdint>
uint8_t hardwarePixelsPerAppPixel = 4;

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <string>
std::string currentFile = "/Users/izmar/samples/Declassified Breaks/britney spears - one more time.wav";

#include <vector>
std::vector<int16_t> sampleDataL;
std::vector<int16_t> sampleDataR;

bool sampleDataHasChanged = false;

void paintWaveformToCanvasIfSampleDataHasChanged()
{
    if (!sampleDataHasChanged)
    {
        return;
    }

    SDL_SetRenderTarget(renderer, canvas);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

    int width, height;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

    int logicalWidth = width / hardwarePixelsPerAppPixel;
    int logicalHeight = height / hardwarePixelsPerAppPixel;

    size_t totalSamples = sampleDataL.size();

    double samplesPerPixel = static_cast<double>(totalSamples) / logicalWidth;

    for (int x = 0; x < logicalWidth; ++x)
    {
        size_t startSample = static_cast<size_t>(x * samplesPerPixel);
        size_t endSample = static_cast<size_t>((x + 1) * samplesPerPixel);
        if (endSample > totalSamples) endSample = totalSamples;

        int16_t minSample = INT16_MAX;
        int16_t maxSample = INT16_MIN;

        for (size_t i = startSample; i < endSample; ++i)
        {
            int16_t s = sampleDataL[i];
            if (s < minSample) minSample = s;
            if (s > maxSample) maxSample = s;
        }

        int y1 = logicalHeight / 2 - (maxSample * logicalHeight / 2) / 32768;
        int y2 = logicalHeight / 2 - (minSample * logicalHeight / 2) / 32768;

        for (int dx = 0; dx < hardwarePixelsPerAppPixel; ++dx)
        {
            for (int dy = 0; dy < hardwarePixelsPerAppPixel; ++dy)
            {
                SDL_RenderLine(
                    renderer,
                    x * hardwarePixelsPerAppPixel + dx,
                    y1 * hardwarePixelsPerAppPixel + dy,
                    x * hardwarePixelsPerAppPixel + dx,
                    y2 * hardwarePixelsPerAppPixel + dy
                );
            }
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
}

void loadSampleData()
{
    ma_result result;
    ma_decoder decoder;

    result = ma_decoder_init_file(currentFile.c_str(), nullptr, &decoder);

    if (result != MA_SUCCESS)
    {
        throw std::runtime_error("Failed to load file: " + currentFile);
    }

    if (decoder.outputFormat != ma_format_s16)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Unsupported format: not s16 PCM");
    }

    if (decoder.outputChannels != 1 && decoder.outputChannels != 2)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Unsupported channel count");
    }

    ma_uint64 frameCount = 0;
    result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

    if (result != MA_SUCCESS)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Failed to get frame count");
    }

    std::vector<int16_t> interleaved(frameCount * decoder.outputChannels);

    ma_uint64 framesRead = 0;
    result = ma_decoder_read_pcm_frames(&decoder, interleaved.data(), frameCount, &framesRead);

    if (result != MA_SUCCESS)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Failed to read PCM frames");
    }

    sampleDataL.clear();
    sampleDataR.clear();

    if (decoder.outputChannels == 1)
    {
        sampleDataL.assign(interleaved.begin(), interleaved.begin() + framesRead);
    }
    else
    {
        sampleDataL.reserve(framesRead);
        sampleDataR.reserve(framesRead);
    
        for (ma_uint64 i = 0; i < framesRead; ++i)
        {
            sampleDataL.push_back(interleaved[i * 2]);
            sampleDataR.push_back(interleaved[i * 2 + 1]);
        }
    }

    ma_decoder_uninit(&decoder);

    if (sampleDataL.size() > 10)
    {
        //for (int i = 0; i < 10; i ++) printf("sample %i, ", sampleDataL[i]);
    }
}

void createCanvas(uint16_t w, uint16_t h)
{
    if (canvas) SDL_DestroyTexture(canvas);

    canvas = SDL_CreateTexture(renderer,
                               SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET,
                               w, h);

    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    SDL_SetAppMetadata("Cupuacu -- A minimalist audio editor by Izmar", "0.1", "nl.izmar.cupuacu");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("", initialDimensions[0], initialDimensions[1], SDL_WINDOW_RESIZABLE, &window, &renderer))
    {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetWindowTitle(window, currentFile.c_str()); 
    SDL_MaximizeWindow(window);
    SDL_RenderPresent(renderer);

    createCanvas(initialDimensions[0], initialDimensions[1]); 

    loadSampleData();

    sampleDataHasChanged = true;

    paintWaveformToCanvasIfSampleDataHasChanged();

    sampleDataHasChanged = false;

    SDL_RenderTexture(renderer, canvas, NULL, NULL);

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    static int iterateCounter = 0;
    //printf("iterate %i\n", iterateCounter++);
    SDL_Delay(16);
    //SDL_Event e;

    //if (SDL_WaitEventTimeout(&e, 16))
    {
        //static int appIterateEventCounter = 0;
        //printf("AppIterate event %i\n", appIterateEventCounter++);
    }

    //paintWaveformToCanvasIfSampleDataHasChanged();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    static int eventCounter = 0;
    //printf("AppEvent %i\n", eventCounter++);
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_RESIZED:
            printf("SDL_EVENT_WINDOW_RESIZED\n");
            break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    printf("quit\n");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
