#include <SDL3/SDL.h>

struct CupuacuState;

void paintWaveformToCanvas(
        SDL_Renderer *renderer,
        SDL_Texture *canvas,
        CupuacuState *state);
