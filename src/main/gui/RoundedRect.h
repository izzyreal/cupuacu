#pragma once

#include <SDL3/SDL.h>
#include <cmath>
#include <vector>

// Internal: quarter filled arc
inline void drawQuarterFilled(SDL_Renderer* renderer,
                              float cx, float cy,
                              float radius,
                              float startAngleDeg,
                              const SDL_Color& col)
{
    constexpr int segments = 16;
    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;

    // Center vertex
    SDL_Vertex center{};
    center.position.x = cx;
    center.position.y = cy;
    SDL_FColor fcol {col.r/255.f, col.g/255.f, col.b/255.f, col.a/255.f};
    center.color = fcol;
    center.tex_coord.x = 0.0f;
    center.tex_coord.y = 0.0f;
    verts.push_back(center);

    // Arc vertices
    for (int i = 0; i <= segments; ++i) {
        float theta = (startAngleDeg + (90.0f * i) / segments) * (float)M_PI / 180.0f;
        float x = cx + std::cos(theta) * radius;
        float y = cy + std::sin(theta) * radius;

        SDL_Vertex v{};
        v.position.x = x;
        v.position.y = y;
        v.color = fcol;
        v.tex_coord.x = 0.0f;
        v.tex_coord.y = 0.0f;
        verts.push_back(v);

        if (i > 0) {
            indices.push_back(0);
            indices.push_back(i);
            indices.push_back(i + 1);
        }
    }

    SDL_RenderGeometry(renderer, nullptr,
                       verts.data(), (int)verts.size(),
                       indices.data(), (int)indices.size());
}

// Internal: quarter outline arc
inline void drawQuarterOutline(SDL_Renderer* renderer,
                               float cx, float cy,
                               float radius,
                               float startAngleDeg,
                               const SDL_Color& col)
{
    constexpr int segments = 24;
    float step = 90.0f / segments;

    float prevX = cx + std::cos(startAngleDeg * M_PI / 180.0f) * radius;
    float prevY = cy + std::sin(startAngleDeg * M_PI / 180.0f) * radius;

    for (int i = 1; i <= segments; ++i) {
        float theta = (startAngleDeg + step * i) * (float)M_PI / 180.0f;
        float x = cx + std::cos(theta) * radius;
        float y = cy + std::sin(theta) * radius;

        SDL_RenderLine(renderer, prevX, prevY, x, y);

        prevX = x;
        prevY = y;
    }
}

// Public: filled rounded rect
inline void drawRoundedRect(SDL_Renderer* renderer,
                            const SDL_FRect& rect,
                            float radius,
                            const SDL_Color& col)
{
    if (radius <= 0.0f) {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        SDL_RenderFillRect(renderer, &rect);
        return;
    }

    radius = std::min(radius, std::min(rect.w, rect.h) / 2.0f);

    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

    // Core rectangles
    SDL_FRect core = { rect.x + radius, rect.y,
                       rect.w - 2 * radius, rect.h };
    SDL_RenderFillRect(renderer, &core);

    SDL_FRect vertical = { rect.x, rect.y + radius,
                           rect.w, rect.h - 2 * radius };
    SDL_RenderFillRect(renderer, &vertical);

    // Four corners
    drawQuarterFilled(renderer, rect.x + radius,         rect.y + radius,         radius, 180.0f, col); // TL
    drawQuarterFilled(renderer, rect.x + rect.w - radius, rect.y + radius,         radius, 270.0f, col); // TR
    drawQuarterFilled(renderer, rect.x + rect.w - radius, rect.y + rect.h - radius, radius,   0.0f, col); // BR
    drawQuarterFilled(renderer, rect.x + radius,         rect.y + rect.h - radius, radius,  90.0f, col); // BL
}

// Public: outline rounded rect
inline void drawRoundedRectOutline(SDL_Renderer* renderer,
                                   const SDL_FRect& rect,
                                   float radius,
                                   const SDL_Color& col)
{
    if (radius <= 0.0f) {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        SDL_RenderRect(renderer, &rect);
        return;
    }

    radius = std::min(radius, std::min(rect.w, rect.h) / 2.0f);

    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

    // Straight edges
    SDL_RenderLine(renderer, rect.x + radius, rect.y,
                             rect.x + rect.w - radius, rect.y); // top
    SDL_RenderLine(renderer, rect.x + rect.w, rect.y + radius,
                             rect.x + rect.w, rect.y + rect.h - radius); // right
    SDL_RenderLine(renderer, rect.x + rect.w - radius, rect.y + rect.h,
                             rect.x + radius, rect.y + rect.h); // bottom
    SDL_RenderLine(renderer, rect.x, rect.y + rect.h - radius,
                             rect.x, rect.y + radius); // left

    // Rounded corners
    drawQuarterOutline(renderer, rect.x + radius,         rect.y + radius,         radius, 180.0f, col); // TL
    drawQuarterOutline(renderer, rect.x + rect.w - radius, rect.y + radius,         radius, 270.0f, col); // TR
    drawQuarterOutline(renderer, rect.x + rect.w - radius, rect.y + rect.h - radius, radius,   0.0f, col); // BR
    drawQuarterOutline(renderer, rect.x + radius,         rect.y + rect.h - radius, radius,  90.0f, col); // BL
}
