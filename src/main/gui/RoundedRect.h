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

inline void drawRoundedRectOutline(SDL_Renderer* renderer,
                                   const SDL_FRect& rect,
                                   float radius,
                                   const SDL_Color& col)
{
    if (radius <= 0.0f) {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        // Subtract 1 from width/height to stay inside rect
        SDL_FRect r = { rect.x, rect.y, rect.w - 1.0f, rect.h - 1.0f };
        SDL_RenderRect(renderer, &r);
        return;
    }

    radius = std::min(radius, std::min(rect.w, rect.h) / 2.0f);

    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

    float x0 = rect.x;
    float y0 = rect.y;
    float x1 = rect.x + rect.w - 1.0f;
    float y1 = rect.y + rect.h - 1.0f;

    // Straight edges
    SDL_RenderLine(renderer, x0 + radius, y0, x1 - radius, y0); // top
    SDL_RenderLine(renderer, x1, y0 + radius, x1, y1 - radius); // right
    SDL_RenderLine(renderer, x1 - radius, y1, x0 + radius, y1); // bottom
    SDL_RenderLine(renderer, x0, y1 - radius, x0, y0 + radius); // left

    // Rounded corners
    drawQuarterOutline(renderer, x0 + radius, y0 + radius, radius, 180.0f, col); // TL
    drawQuarterOutline(renderer, x1 - radius, y0 + radius, radius, 270.0f, col); // TR
    drawQuarterOutline(renderer, x1 - radius, y1 - radius, radius,   0.0f, col); // BR
    drawQuarterOutline(renderer, x0 + radius, y1 - radius, radius,  90.0f, col); // BL
}

// Top corners only
inline void drawTopRoundedRect(SDL_Renderer* renderer,
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

    // Core rectangle covering bottom and middle
    SDL_FRect core = { rect.x, rect.y + radius, rect.w, rect.h - radius };
    SDL_RenderFillRect(renderer, &core);

    // Top horizontal bar (between corners)
    SDL_FRect topBar = { rect.x + radius, rect.y, rect.w - 2*radius, radius };
    SDL_RenderFillRect(renderer, &topBar);

    // Top corners
    drawQuarterFilled(renderer, rect.x + radius, rect.y + radius, radius, 180.0f, col); // TL
    drawQuarterFilled(renderer, rect.x + rect.w - radius, rect.y + radius, radius, 270.0f, col); // TR
}

// Bottom corners only
inline void drawBottomRoundedRect(SDL_Renderer* renderer,
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

    // Core rectangle covering top and middle
    SDL_FRect core = { rect.x, rect.y, rect.w, rect.h - radius };
    SDL_RenderFillRect(renderer, &core);

    // Bottom horizontal bar (between corners)
    SDL_FRect bottomBar = { rect.x + radius, rect.y + rect.h - radius, rect.w - 2*radius, radius };
    SDL_RenderFillRect(renderer, &bottomBar);

    // Bottom corners
    drawQuarterFilled(renderer, rect.x + rect.w - radius, rect.y + rect.h - radius, radius, 0.0f, col); // BR
    drawQuarterFilled(renderer, rect.x + radius, rect.y + rect.h - radius, radius, 90.0f, col); // BL
}

// Top corners only outline (no bottom edge)
inline void drawTopRoundedRectOutline(SDL_Renderer* renderer,
                                      const SDL_FRect& rect,
                                      float radius,
                                      const SDL_Color& col)
{
    if (radius <= 0.0f) {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        SDL_RenderLine(renderer, rect.x, rect.y, rect.x + rect.w - 1.0f, rect.y); // top
        SDL_RenderLine(renderer, rect.x, rect.y, rect.x, rect.y + rect.h - 1.0f); // left
        SDL_RenderLine(renderer, rect.x + rect.w - 1.0f, rect.y, rect.x + rect.w - 1.0f, rect.y + rect.h - 1.0f); // right
        return;
    }

    radius = std::min(radius, std::min(rect.w, rect.h)/2.0f);
    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

    float x0 = rect.x;
    float y0 = rect.y;
    float x1 = rect.x + rect.w - 1.0f;
    float y1 = rect.y + rect.h - 1.0f;

    // Top edge
    SDL_RenderLine(renderer, x0 + radius, y0, x1 - radius, y0);

    // Vertical edges
    SDL_RenderLine(renderer, x0, y0 + radius, x0, y1);
    SDL_RenderLine(renderer, x1, y0 + radius, x1, y1);

    // Top corners
    drawQuarterOutline(renderer, x0 + radius, y0 + radius, radius, 180.0f, col); // TL
    drawQuarterOutline(renderer, x1 - radius, y0 + radius, radius, 270.0f, col); // TR
}

// Bottom corners only outline (no top edge)
inline void drawBottomRoundedRectOutline(SDL_Renderer* renderer,
                                         const SDL_FRect& rect,
                                         float radius,
                                         const SDL_Color& col)
{
    if (radius <= 0.0f) {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        SDL_RenderLine(renderer, rect.x, rect.y + rect.h - 1.0f, rect.x + rect.w - 1.0f, rect.y + rect.h - 1.0f); // bottom
        SDL_RenderLine(renderer, rect.x, rect.y, rect.x, rect.y + rect.h - 1.0f); // left
        SDL_RenderLine(renderer, rect.x + rect.w - 1.0f, rect.y, rect.x + rect.w - 1.0f, rect.y + rect.h - 1.0f); // right
        return;
    }

    radius = std::min(radius, std::min(rect.w, rect.h)/2.0f);
    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

    float x0 = rect.x;
    float y0 = rect.y;
    float x1 = rect.x + rect.w - 1.0f;
    float y1 = rect.y + rect.h - 1.0f;

    // Bottom edge
    SDL_RenderLine(renderer, x0 + radius, y1, x1 - radius, y1);

    // Vertical edges
    SDL_RenderLine(renderer, x0, y0, x0, y1 - radius);
    SDL_RenderLine(renderer, x1, y0, x1, y1 - radius);

    // Bottom corners
    drawQuarterOutline(renderer, x1 - radius, y1 - radius, radius, 0.0f, col); // BR
    drawQuarterOutline(renderer, x0 + radius, y1 - radius, radius, 90.0f, col); // BL
}

// Left and right edges only (no top/bottom edges)
inline void drawVerticalEdges(SDL_Renderer* renderer,
                              const SDL_FRect& rect,
                              const SDL_Color& col)
{
    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
    SDL_RenderLine(renderer, rect.x, rect.y, rect.x, rect.y + rect.h - 1.0f);           // left
    SDL_RenderLine(renderer, rect.x + rect.w - 1.0f, rect.y, rect.x + rect.w - 1.0f, rect.y + rect.h - 1.0f); // right
}
