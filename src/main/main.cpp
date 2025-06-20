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

static const double INITIAL_SAMPLES_PER_PIXEL = 1;
static const double INITIAL_VERTICAL_ZOOM = 1;
static const int64_t INITIAL_SAMPLE_OFFSET = 0;

double samplesPerPixel = 1;
double verticalZoom = 1;
int64_t sampleOffset = 0;

void paintWaveformToCanvas()
{
    SDL_SetRenderTarget(renderer, canvas);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

    int width, height;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

    bool noMoreStuffToDraw = false;

//    printf("=============\n"); 

    for (int x = 0; x < width; ++x)
    {
        if (noMoreStuffToDraw)
        {
            // Should probably be replaced with implementation of incapacity
            // of zooming to this kind of level. In other words, the waveform
            // always covers the width of the window, or overflows it. It
            // never underflows. But for now, this is easier.
            break;
        }

        const size_t startSample = static_cast<size_t>(x * samplesPerPixel) + sampleOffset;
        size_t endSample = static_cast<size_t>((x + 1) * samplesPerPixel) + sampleOffset;

        if (endSample == startSample) endSample++;

        //if (endSample > totalSamples) endSample = totalSamples;

        int16_t minSample = INT16_MAX;
        int16_t maxSample = INT16_MIN;

        //printf("startSample: %i, endSample: %i\n", startSample, endSample);

        for (size_t i = startSample; i < endSample; ++i)
        {
            if (i >= sampleDataL.size())
            {
                noMoreStuffToDraw = true;
                break;
            }

            const int16_t s = sampleDataL[i];
            if (s < minSample) minSample = s;
            if (s > maxSample) maxSample = s;
        }

        int y1 = height / 2 - (maxSample * verticalZoom * height / 2) / 32768;
        int y2 = height / 2 - (minSample * verticalZoom * height / 2) / 32768;

        y1 = std::clamp<int>(y1, -1, height);
        y2 = std::clamp<int>(y2, -1, height);

 //       printf("y1: %i, y2: %i\n", y1, y2);

        SDL_RenderLine(renderer, x, y1, x, y2);
    }

    SDL_SetRenderTarget(renderer, NULL);
}

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
    if (canvas) SDL_DestroyTexture(canvas);

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

    if (!SDL_CreateWindowAndRenderer("", initialDimensions[0], initialDimensions[1], SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window, &renderer))
    {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    loadSampleData();

    SDL_SetWindowTitle(window, currentFile.c_str()); 
    SDL_RenderPresent(renderer);

    const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions();

    printf("desired canvas width: %i\n", newCanvasDimensions.x);

    createCanvas(newCanvasDimensions);

    SDL_FPoint actualCanvasDimensions;

    SDL_GetTextureSize(canvas, &actualCanvasDimensions.x, &actualCanvasDimensions.y);

    printf("actual canvas width: %f\n", actualCanvasDimensions.x);

    samplesPerPixel = sampleDataL.size() / double(newCanvasDimensions.x);
    printf("samplesPerPixel during init: %f\n", samplesPerPixel);
    paintWaveformToCanvas();
    renderCanvasToWindow();

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
            {
                //printf("SDL_EVENT_WINDOW_RESIZED\n");
                SDL_FPoint currentCanvasDimensions;
                SDL_GetTextureSize(canvas, &currentCanvasDimensions.x, &currentCanvasDimensions.y);
                const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions();
                const auto currentCanvasWidth = static_cast<uint16_t>(currentCanvasDimensions.x);
                const auto currentCanvasHeight = static_cast<uint16_t>(currentCanvasDimensions.y);

                if (currentCanvasWidth != newCanvasDimensions.x ||
                        currentCanvasHeight != newCanvasDimensions.y)
                {
                    createCanvas(newCanvasDimensions);
                    paintWaveformToCanvas();
                }

                renderCanvasToWindow();
                break;
            }
        case SDL_EVENT_KEY_DOWN:
            uint8_t multiplier = 1;

            if (event->key.scancode == SDL_SCANCODE_ESCAPE)
            {
                    SDL_FPoint canvasDimensions;
                    SDL_GetTextureSize(canvas, &canvasDimensions.x, &canvasDimensions.y);
                    samplesPerPixel = sampleDataL.size() / canvasDimensions.x;
                    printf("new samplesPerPixel: %f\n", samplesPerPixel);
                    verticalZoom = INITIAL_VERTICAL_ZOOM;
                    sampleOffset = INITIAL_SAMPLE_OFFSET;
                    paintWaveformToCanvas();
                    renderCanvasToWindow();
                    break;
            }
            
            if (event->key.mod & SDL_KMOD_SHIFT) multiplier *= 2;
            if (event->key.mod & SDL_KMOD_ALT) multiplier *= 2;
            if (event->key.mod & SDL_KMOD_CTRL) multiplier *= 2;

            if (event->key.scancode == SDL_SCANCODE_Q)
            {
                // Should be replaced with implemention of incapacity to underflow horizontally.
                if (samplesPerPixel < sampleDataL.size() / 2)
                {
                    samplesPerPixel *= 2;
                    paintWaveformToCanvas();
                    renderCanvasToWindow();
                }
            }
            else if (event->key.scancode == SDL_SCANCODE_W)
            {
                if (samplesPerPixel > 0.1)
                {
                    samplesPerPixel /= 2;
                    paintWaveformToCanvas();
                    renderCanvasToWindow();
                }
            }
            else if (event->key.scancode == SDL_SCANCODE_E)
            {
                verticalZoom -= 0.3 * multiplier;
                if (verticalZoom < 1) verticalZoom = 1;
                paintWaveformToCanvas();
                renderCanvasToWindow();
            }
            else if (event->key.scancode == SDL_SCANCODE_R)
            {
                    verticalZoom += 0.3 * multiplier;
                    paintWaveformToCanvas();
                    renderCanvasToWindow();
            }
            else if (event->key.scancode == SDL_SCANCODE_LEFT)
            {
                if (sampleOffset > 0)
                {
                    sampleOffset -= std::max(samplesPerPixel, 1.0) * multiplier;
                    if (sampleOffset < 0) sampleOffset = 0;
                    paintWaveformToCanvas();
                    renderCanvasToWindow();
                }
            }
            else if (event->key.scancode == SDL_SCANCODE_RIGHT)
            {
                if (sampleOffset < sampleDataL.size())
                {
                    sampleOffset += std::max(samplesPerPixel, 1.0) * multiplier;
                    paintWaveformToCanvas();
                    renderCanvasToWindow();
                }
            }
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
