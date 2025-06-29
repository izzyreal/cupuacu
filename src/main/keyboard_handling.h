#include <SDL3/SDL.h>

#include <functional>

static void handleKeyDown(
        SDL_Event *event,
        SDL_Texture *canvas,
        double &samplesPerPixel,
        double &verticalZoom,
        uint64_t &sampleOffset,
        const std::vector<int16_t> &sampleDataL,
        const double INITIAL_VERTICAL_ZOOM,
        const uint64_t INITIAL_SAMPLE_OFFSET,
        std::function<void()> &paintAndRenderWaveform
        )
{
    uint8_t multiplier = 1;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        SDL_FPoint canvasDimensions;
        SDL_GetTextureSize(canvas, &canvasDimensions.x, &canvasDimensions.y);
        samplesPerPixel = sampleDataL.size() / canvasDimensions.x;
        verticalZoom = INITIAL_VERTICAL_ZOOM;
        sampleOffset = INITIAL_SAMPLE_OFFSET;
        paintAndRenderWaveform();
        return;
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
            return;
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
            return;
        }

        sampleOffset += std::max(samplesPerPixel, 1.0) * multiplier;
        paintAndRenderWaveform();
    }
}

