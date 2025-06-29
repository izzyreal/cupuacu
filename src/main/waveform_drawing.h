#include <SDL3/SDL.h>

#include <vector>

void paintWaveformToCanvas(
        SDL_Renderer *renderer,
        SDL_Texture *canvas,
        const std::vector<int16_t> &sampleDataL,
        const double samplesPerPixel,
        const double verticalZoom,
        const int64_t sampleOffset);
