#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "CupuacuState.h"

#include "waveform_drawing.h"
#include "keyboard_handling.h"
#include "mouse_handling.h"
#include "text.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *canvas = NULL;

const uint16_t initialDimensions[] = { 1280, 720 };

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static const double INITIAL_SAMPLES_PER_PIXEL = 1;
static const double INITIAL_VERTICAL_ZOOM = 1;
static const int64_t INITIAL_SAMPLE_OFFSET = 0;

#include "gui/Component.h"

const std::function<void(CupuacuState*)> renderCanvasToWindow = [](CupuacuState *state)
{
    SDL_FPoint currentCanvasDimensions;
    SDL_GetTextureSize(canvas, &currentCanvasDimensions.x, &currentCanvasDimensions.y);
    SDL_FRect dstRect;
    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.w = currentCanvasDimensions.x * state->hardwarePixelsPerAppPixel;
    dstRect.h = currentCanvasDimensions.y * state->hardwarePixelsPerAppPixel;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    renderText(renderer, canvas, "File  View");
    SDL_RenderTexture(renderer, canvas, NULL, &dstRect);
    SDL_RenderPresent(renderer);
};

const std::function<void(CupuacuState*)> paintWaveform = [](CupuacuState *state)
{
    paintWaveformToCanvas(
            renderer,
            canvas,
            state);
};

const std::function<void(CupuacuState*)> paintAndRenderWaveform = [](CupuacuState *state)
{
    paintWaveform(state);
    renderCanvasToWindow(state);
};

void loadSampleData(CupuacuState *state)
{
    ma_result result;
    ma_decoder decoder;

    result = ma_decoder_init_file(state->currentFile.c_str(), nullptr, &decoder);

    if (result != MA_SUCCESS)
    {
        throw std::runtime_error("Failed to load file: " + state->currentFile);
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

    state->sampleDataL.clear();
    state->sampleDataR.clear();

    if (decoder.outputChannels == 1)
    {
        state->sampleDataL.assign(interleaved.begin(), interleaved.begin() + framesRead);
    }
    else
    {
        state->sampleDataL.reserve(framesRead);
        state->sampleDataR.reserve(framesRead);
    
        for (ma_uint64 i = 0; i < framesRead; ++i)
        {
            state->sampleDataL.push_back(interleaved[i * 2]);
            state->sampleDataR.push_back(interleaved[i * 2 + 1]);
        }
    }

    ma_decoder_uninit(&decoder);
}

SDL_Point computeDesiredCanvasDimensions(const uint8_t hardwarePixelsPerAppPixel)
{
    SDL_Point result;

    if (!SDL_GetWindowSizeInPixels(window, &result.x, &result.y))
    {
        return {0,0};
    }

    result.x = std::floor(result.x / hardwarePixelsPerAppPixel);
    result.y = std::floor(result.y / hardwarePixelsPerAppPixel);

    return result;
}

void createCanvas(const SDL_Point &dimensions)
{
    if (canvas)
    {
        SDL_DestroyTexture(canvas);
    }

    canvas = SDL_CreateTexture(renderer,
                               SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET,
                               dimensions.x, dimensions.y);
    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    CupuacuState *state = new CupuacuState();

    *appstate = state;

    SDL_SetAppMetadata("Cupuacu -- A minimalist audio editor by Izmar", "0.1", "nl.izmar.cupuacu");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!TTF_Init())
    {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(
                "",
                initialDimensions[0],
                initialDimensions[1],
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                &window,
                &renderer)
            )
    {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    loadSampleData(state);

    SDL_SetWindowTitle(window, state->currentFile.c_str()); 
    SDL_RenderPresent(renderer);

    const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions(state->hardwarePixelsPerAppPixel);

    createCanvas(newCanvasDimensions);

    SDL_FPoint actualCanvasDimensions;

    SDL_GetTextureSize(canvas, &actualCanvasDimensions.x, &actualCanvasDimensions.y);

    state->samplesPerPixel = state->sampleDataL.size() / double(newCanvasDimensions.x);

    paintAndRenderWaveform(state);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    CupuacuState *state = (CupuacuState*) appstate;

    if (state->samplesToScroll != 0.0f)
    {
        const int64_t scroll = static_cast<int64_t>(state->samplesToScroll);
        const uint64_t oldOffset = state->sampleOffset;

        if (scroll < 0)
        {
            uint64_t absScroll = static_cast<uint64_t>(-scroll);

            state->sampleOffset = (state->sampleOffset > absScroll)
                ? state->sampleOffset - absScroll
                : 0;

            state->selectionEndSample = (state->selectionEndSample > absScroll)
                ? state->selectionEndSample - absScroll
                : 0;
        }
        else
        {
            state->sampleOffset += static_cast<uint64_t>(scroll);
            state->selectionEndSample += static_cast<uint64_t>(scroll);
        }

        if (oldOffset != state->sampleOffset)
        {
            paintAndRenderWaveform(state);
        }
    }

    SDL_Delay(16);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    CupuacuState *state = (CupuacuState*)appstate;
    switch (event->type)
    {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_RESIZED:
            {
                SDL_FPoint currentCanvasDimensions;
                SDL_GetTextureSize(canvas, &currentCanvasDimensions.x, &currentCanvasDimensions.y);
                const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions(state->hardwarePixelsPerAppPixel);
                const auto currentCanvasWidth = static_cast<uint16_t>(currentCanvasDimensions.x);
                const auto currentCanvasHeight = static_cast<uint16_t>(currentCanvasDimensions.y);

                if (currentCanvasWidth != newCanvasDimensions.x ||
                        currentCanvasHeight != newCanvasDimensions.y)
                {
                    createCanvas(newCanvasDimensions);
                    paintAndRenderWaveform(state);
                }
                else
                {
                    renderCanvasToWindow(state);
                }
                break;
            }
        case SDL_EVENT_KEY_DOWN:
            handleKeyDown(
                    event,
                    canvas,
                    state,
                    INITIAL_VERTICAL_ZOOM,
                    INITIAL_SAMPLE_OFFSET,
                    paintAndRenderWaveform
                    );
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            handleMouseEvent(event, renderer, canvas, window, paintWaveform, renderCanvasToWindow, state);
            break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

