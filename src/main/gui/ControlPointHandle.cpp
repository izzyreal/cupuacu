#include "ControlPointHandle.hpp"

using namespace cupuacu::gui;

void ControlPointHandle::onDraw(SDL_Renderer *renderer)
{
    const SDL_Color color = isMouseOver() || active ? activeColor : idleColor;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect rectToFill{0, 0, static_cast<float>(getWidth()),
                               static_cast<float>(getHeight())};
    SDL_RenderFillRect(renderer, &rectToFill);
}
