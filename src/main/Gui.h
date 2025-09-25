#pragma once

#include <SDL3/SDL.h>

struct CupuacuState;

SDL_Point computeRequiredCanvasDimensions(SDL_Window *window, const uint8_t pixelScale);
void createCanvas(CupuacuState *state, const SDL_Point &dimensions);
SDL_Rect getWaveformRect(const uint16_t canvasWidth, const uint16_t canvasHeight, const uint8_t pixelScale, const uint8_t menuHeight);
SDL_Rect getMenuBarRect(const uint16_t canvasWidth, const uint16_t canvasHeight, const uint8_t pixelScale, const uint8_t menuFontSize);
SDL_Rect getStatusBarRect(const uint16_t canvasWidth, const uint16_t canvasHeight, const uint8_t pixelScale, const uint8_t menuFontSize);
void resizeComponents(CupuacuState *state);
void rebuildWaveforms(CupuacuState *state);
void resizeWaveforms(CupuacuState *state);
void buildComponents(CupuacuState *state);
