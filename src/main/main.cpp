#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "waveform_drawing.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *canvas = NULL;

const uint16_t initialDimensions[] = { 1280, 720 };

#include <cstdint>
uint8_t hardwarePixelsPerAppPixel = 6;

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <string>
std::string currentFile = "/Users/izmar/Downloads/ams_chill.wav";

#include <vector>

std::vector<int16_t> sampleDataL;
std::vector<int16_t> sampleDataR;

static const double INITIAL_SAMPLES_PER_PIXEL = 1;
static const double INITIAL_VERTICAL_ZOOM = 1;
static const int64_t INITIAL_SAMPLE_OFFSET = 0;

double samplesPerPixel = 1;
double verticalZoom = 1;
int64_t sampleOffset = 0;

void renderCanvasToWindow()
{
    SDL_FPoint currentCanvasDimensions;
    SDL_GetTextureSize(canvas, &currentCanvasDimensions.x, &currentCanvasDimensions.y);
    SDL_FRect dstRect;
    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.w = currentCanvasDimensions.x * hardwarePixelsPerAppPixel;
    dstRect.h = currentCanvasDimensions.y * hardwarePixelsPerAppPixel;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, canvas, NULL, &dstRect);
    SDL_RenderPresent(renderer);
}

void paintAndRenderWaveform()
{
    paintWaveformToCanvas(
            renderer,
            canvas,
            sampleDataL,
            samplesPerPixel,
            verticalZoom,
            sampleOffset);
    renderCanvasToWindow();
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
}

SDL_Point computeDesiredCanvasDimensions()
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
    SDL_SetAppMetadata("Cupuacu -- A minimalist audio editor by Izmar", "0.1", "nl.izmar.cupuacu");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
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

    loadSampleData();

    SDL_SetWindowTitle(window, currentFile.c_str()); 
    SDL_RenderPresent(renderer);

    const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions();

    createCanvas(newCanvasDimensions);

    SDL_FPoint actualCanvasDimensions;

    SDL_GetTextureSize(canvas, &actualCanvasDimensions.x, &actualCanvasDimensions.y);

    samplesPerPixel = sampleDataL.size() / double(newCanvasDimensions.x);

    paintAndRenderWaveform();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    SDL_Delay(16);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    switch (event->type)
    {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_RESIZED:
            {
                SDL_FPoint currentCanvasDimensions;
                SDL_GetTextureSize(canvas, &currentCanvasDimensions.x, &currentCanvasDimensions.y);
                const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions();
                const auto currentCanvasWidth = static_cast<uint16_t>(currentCanvasDimensions.x);
                const auto currentCanvasHeight = static_cast<uint16_t>(currentCanvasDimensions.y);

                if (currentCanvasWidth != newCanvasDimensions.x ||
                        currentCanvasHeight != newCanvasDimensions.y)
                {
                    createCanvas(newCanvasDimensions);
                    paintAndRenderWaveform();
                }
                else
                {
                    renderCanvasToWindow();
                }
                break;
            }
        case SDL_EVENT_KEY_DOWN:
            uint8_t multiplier = 1;

            if (event->key.scancode == SDL_SCANCODE_ESCAPE)
            {
                    SDL_FPoint canvasDimensions;
                    SDL_GetTextureSize(canvas, &canvasDimensions.x, &canvasDimensions.y);
                    samplesPerPixel = sampleDataL.size() / canvasDimensions.x;
                    verticalZoom = INITIAL_VERTICAL_ZOOM;
                    sampleOffset = INITIAL_SAMPLE_OFFSET;
                    paintAndRenderWaveform();
                    break;
            }
            
            if (event->key.mod & SDL_KMOD_SHIFT) multiplier *= 2;
            if (event->key.mod & SDL_KMOD_ALT) multiplier *= 2;
            if (event->key.mod & SDL_KMOD_CTRL) multiplier *= 2;

            if (event->key.scancode == SDL_SCANCODE_Q)
            {
                if (samplesPerPixel < static_cast<float>(sampleDataL.size()) / 2.f)
                {
                    samplesPerPixel *= 2.f;
                    paintAndRenderWaveform();
                }
            }
            else if (event->key.scancode == SDL_SCANCODE_W)
            {
                if (samplesPerPixel > 0.01)
                {
                    samplesPerPixel /= 2.f;
                    paintAndRenderWaveform();
                }
            }
            else if (event->key.scancode == SDL_SCANCODE_E)
            {
                verticalZoom -= 0.3 * multiplier;

                if (verticalZoom < 1)
                {
                    verticalZoom = 1;
                }
                
                paintAndRenderWaveform();
            }
            else if (event->key.scancode == SDL_SCANCODE_R)
            {
                    verticalZoom += 0.3 * multiplier;

                    paintAndRenderWaveform();
            }
            else if (event->key.scancode == SDL_SCANCODE_LEFT)
            {
                if (sampleOffset == 0)
                {
                    break;
                }

                sampleOffset -= std::max(samplesPerPixel, 1.0) * multiplier;
                
                if (sampleOffset < 0)
                {
                    sampleOffset = 0;
                }

                paintAndRenderWaveform();
            }
            else if (event->key.scancode == SDL_SCANCODE_RIGHT)
            {
                if (sampleOffset >= sampleDataL.size())
                {
                    break;
                }

                sampleOffset += std::max(samplesPerPixel, 1.0) * multiplier;
                paintAndRenderWaveform();
            }

            break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

