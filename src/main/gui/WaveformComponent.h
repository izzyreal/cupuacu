#pragma once
#include "Component.h"
#include "../CupuacuState.h"
#include <SDL3/SDL.h>

struct WaveformComponent : Component {
    CupuacuState* state = nullptr;
    WaveformComponent(SDL_Rect r, CupuacuState* s) { rect = r; state = s; }
    void onDraw(SDL_Renderer* renderer) override;
    bool onHandleEvent(const SDL_Event &e) override;
    void timerCallback() override;

    private:
    void handleScroll(const SDL_Event&);
};
