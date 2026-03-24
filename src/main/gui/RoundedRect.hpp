#pragma once

#include <SDL3/SDL.h>
#include <cmath>
#include <vector>

#include "RoundedRectPlanning.hpp"

#ifndef M_PI
#define M_PI  (3.14159265)
#endif

namespace cupuacu::gui
{
    inline void drawRoundedRect(SDL_Renderer *renderer, const SDL_FRect &rect,
                                float radius, const SDL_Color &col);
    inline void drawTopRoundedRect(SDL_Renderer *renderer,
                                   const SDL_FRect &rect, float radius,
                                   const SDL_Color &col);
    inline void drawBottomRoundedRect(SDL_Renderer *renderer,
                                      const SDL_FRect &rect, float radius,
                                      const SDL_Color &col);

    inline float snapRoundedRectValueToPixelGrid(const uint8_t pixelScale,
                                                 const float value)
    {
        const float step =
            1.0f / std::max(1, static_cast<int>(pixelScale));
        return std::round(value / step) * step;
    }

    inline SDL_FRect snapRoundedRectToPixelGrid(const SDL_FRect &rect,
                                                const uint8_t pixelScale)
    {
        const float x0 =
            snapRoundedRectValueToPixelGrid(pixelScale, rect.x);
        const float y0 =
            snapRoundedRectValueToPixelGrid(pixelScale, rect.y);
        const float x1 = snapRoundedRectValueToPixelGrid(
            pixelScale, rect.x + rect.w);
        const float y1 = snapRoundedRectValueToPixelGrid(
            pixelScale, rect.y + rect.h);
        return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }

    inline float snapRoundedRectRadiusToPixelGrid(const float radius,
                                                  const uint8_t pixelScale)
    {
        return std::max(
            0.0f, snapRoundedRectValueToPixelGrid(pixelScale, radius));
    }

    inline int toRoundedRectGridUnits(const float value,
                                      const uint8_t pixelScale)
    {
        return static_cast<int>(std::lround(
            value * std::max(1, static_cast<int>(pixelScale))));
    }

    inline float fromRoundedRectGridUnits(const int value,
                                          const uint8_t pixelScale)
    {
        return static_cast<float>(value) /
               std::max(1, static_cast<int>(pixelScale));
    }

    inline int computeRoundedRectRowInset(const int y, const int height,
                                          const int radius)
    {
        if (radius <= 0)
        {
            return 0;
        }

        const int distanceFromTop = y;
        const int distanceFromBottom = height - 1 - y;
        const int edgeDistance =
            std::min(distanceFromTop, distanceFromBottom);
        if (edgeDistance >= radius)
        {
            return 0;
        }

        const float center = radius - 0.5f;
        const float sampleY = edgeDistance + 0.5f;
        const float dy = std::fabs(center - sampleY);
        const float span =
            std::sqrt(std::max(0.0f, center * center - dy * dy));
        const int inset = static_cast<int>(std::ceil(center - span));
        return std::clamp(inset, 0, radius);
    }

    inline void fillRoundedRectGridRows(SDL_Renderer *renderer, const int x,
                                        const int y, const int width,
                                        const int height, const int radius,
                                        const uint8_t pixelScale)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        for (int row = 0; row < height; ++row)
        {
            const int inset = computeRoundedRectRowInset(row, height, radius);
            const int rowX = x + inset;
            const int rowWidth = width - inset * 2;
            if (rowWidth <= 0)
            {
                continue;
            }

            const SDL_FRect rowRect{
                fromRoundedRectGridUnits(rowX, pixelScale),
                fromRoundedRectGridUnits(y + row, pixelScale),
                fromRoundedRectGridUnits(rowWidth, pixelScale),
                fromRoundedRectGridUnits(1, pixelScale)};
            SDL_RenderFillRect(renderer, &rowRect);
        }
    }

    inline int computeRoundedRectRowInsetForFlags(const int row,
                                                  const int height,
                                                  const int radius,
                                                  const bool roundTop,
                                                  const bool roundBottom)
    {
        if (radius <= 0 || height <= 0)
        {
            return 0;
        }

        int inset = 0;
        if (roundTop && row < radius)
        {
            inset = std::max(
                inset,
                computeRoundedRectRowInset(row, radius * 2, radius));
        }

        if (roundBottom && row >= height - radius)
        {
            inset = std::max(
                inset,
                computeRoundedRectRowInset(height - 1 - row, radius * 2,
                                           radius));
        }

        return inset;
    }

    inline bool getRoundedRectRowSpan(const int x, const int width,
                                      const int height, const int radius,
                                      const bool roundTop,
                                      const bool roundBottom, const int row,
                                      int &left, int &spanWidth)
    {
        if (width <= 0 || height <= 0 || row < 0 || row >= height)
        {
            return false;
        }

        const int inset = computeRoundedRectRowInsetForFlags(
            row, height, radius, roundTop, roundBottom);
        left = x + inset;
        spanWidth = width - inset * 2;
        return spanWidth > 0;
    }

    inline void fillRoundedRectGridSegment(SDL_Renderer *renderer,
                                           const int x0, const int width,
                                           const int y,
                                           const uint8_t pixelScale)
    {
        if (width <= 0)
        {
            return;
        }

        const SDL_FRect rowRect{
            fromRoundedRectGridUnits(x0, pixelScale),
            fromRoundedRectGridUnits(y, pixelScale),
            fromRoundedRectGridUnits(width, pixelScale),
            fromRoundedRectGridUnits(1, pixelScale)};
        SDL_RenderFillRect(renderer, &rowRect);
    }

    inline void drawRoundedRectPixelPerfect(SDL_Renderer *renderer,
                                            const SDL_FRect &rect, float radius,
                                            const SDL_Color &col,
                                            const uint8_t pixelScale)
    {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        const int x = toRoundedRectGridUnits(rect.x, pixelScale);
        const int y = toRoundedRectGridUnits(rect.y, pixelScale);
        const int width = std::max(
            0, toRoundedRectGridUnits(rect.w, pixelScale));
        const int height = std::max(
            0, toRoundedRectGridUnits(rect.h, pixelScale));
        const int radiusUnits = std::clamp(
            toRoundedRectGridUnits(radius, pixelScale), 0,
            std::min(width, height) / 2);

        fillRoundedRectGridRows(renderer, x, y, width, height, radiusUnits,
                                pixelScale);
    }

    inline void drawTopRoundedRectPixelPerfect(SDL_Renderer *renderer,
                                               const SDL_FRect &rect,
                                               float radius,
                                               const SDL_Color &col,
                                               const uint8_t pixelScale)
    {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        const int x = toRoundedRectGridUnits(rect.x, pixelScale);
        const int y = toRoundedRectGridUnits(rect.y, pixelScale);
        const int width = std::max(
            0, toRoundedRectGridUnits(rect.w, pixelScale));
        const int height = std::max(
            0, toRoundedRectGridUnits(rect.h, pixelScale));
        const int radiusUnits = std::clamp(
            toRoundedRectGridUnits(radius, pixelScale), 0,
            std::min(width, height) / 2);

        if (width <= 0 || height <= 0)
        {
            return;
        }

        for (int row = 0; row < height; ++row)
        {
            const int inset =
                row < radiusUnits
                    ? computeRoundedRectRowInset(row, radiusUnits * 2,
                                                 radiusUnits)
                    : 0;
            const int rowX = x + inset;
            const int rowWidth = width - inset * 2;
            if (rowWidth <= 0)
            {
                continue;
            }

            const SDL_FRect rowRect{
                fromRoundedRectGridUnits(rowX, pixelScale),
                fromRoundedRectGridUnits(y + row, pixelScale),
                fromRoundedRectGridUnits(rowWidth, pixelScale),
                fromRoundedRectGridUnits(1, pixelScale)};
            SDL_RenderFillRect(renderer, &rowRect);
        }
    }

    inline void drawRoundedRectBorderPixelPerfect(SDL_Renderer *renderer,
                                                  const SDL_FRect &rect,
                                                  float radius,
                                                  const SDL_Color &col,
                                                  const uint8_t pixelScale,
                                                  const float borderThickness,
                                                  const bool roundTop,
                                                  const bool roundBottom)
    {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        const int outerX = toRoundedRectGridUnits(rect.x, pixelScale);
        const int outerY = toRoundedRectGridUnits(rect.y, pixelScale);
        const int outerWidth = std::max(
            0, toRoundedRectGridUnits(rect.w, pixelScale));
        const int outerHeight = std::max(
            0, toRoundedRectGridUnits(rect.h, pixelScale));
        const int outerRadius = std::clamp(
            toRoundedRectGridUnits(radius, pixelScale), 0,
            std::min(outerWidth, outerHeight) / 2);
        const int borderUnits = std::max(
            1, toRoundedRectGridUnits(borderThickness, pixelScale));

        if (outerWidth <= 0 || outerHeight <= 0)
        {
            return;
        }

        const int innerX = outerX + borderUnits;
        const int innerY = outerY + (roundTop ? borderUnits : 0);
        const int innerWidth = std::max(0, outerWidth - borderUnits * 2);
        const int innerHeight = std::max(
            0, outerHeight - (roundTop ? borderUnits : 0) -
                   (roundBottom ? borderUnits : 0));
        const int innerRadius = std::clamp(
            outerRadius - borderUnits, 0,
            std::min(innerWidth, innerHeight) / 2);

        for (int row = 0; row < outerHeight; ++row)
        {
            int outerLeft = 0;
            int outerSpanWidth = 0;
            if (!getRoundedRectRowSpan(outerX, outerWidth, outerHeight,
                                       outerRadius, roundTop, roundBottom, row,
                                       outerLeft, outerSpanWidth))
            {
                continue;
            }

            const int canvasY = outerY + row;
            const int innerRow = canvasY - innerY;
            int innerLeft = 0;
            int innerSpanWidth = 0;
            const bool hasInner = getRoundedRectRowSpan(
                innerX, innerWidth, innerHeight, innerRadius, roundTop,
                roundBottom, innerRow, innerLeft, innerSpanWidth);

            if (!hasInner)
            {
                fillRoundedRectGridSegment(renderer, outerLeft, outerSpanWidth,
                                           canvasY, pixelScale);
                continue;
            }

            fillRoundedRectGridSegment(renderer, outerLeft,
                                       innerLeft - outerLeft, canvasY,
                                       pixelScale);
            const int outerRightExclusive = outerLeft + outerSpanWidth;
            const int innerRightExclusive = innerLeft + innerSpanWidth;
            fillRoundedRectGridSegment(renderer, innerRightExclusive,
                                       outerRightExclusive -
                                           innerRightExclusive,
                                       canvasY, pixelScale);
        }
    }

    inline void drawBottomRoundedRectPixelPerfect(SDL_Renderer *renderer,
                                                  const SDL_FRect &rect,
                                                  float radius,
                                                  const SDL_Color &col,
                                                  const uint8_t pixelScale)
    {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        const int x = toRoundedRectGridUnits(rect.x, pixelScale);
        const int y = toRoundedRectGridUnits(rect.y, pixelScale);
        const int width = std::max(
            0, toRoundedRectGridUnits(rect.w, pixelScale));
        const int height = std::max(
            0, toRoundedRectGridUnits(rect.h, pixelScale));
        const int radiusUnits = std::clamp(
            toRoundedRectGridUnits(radius, pixelScale), 0,
            std::min(width, height) / 2);

        if (width <= 0 || height <= 0)
        {
            return;
        }

        for (int row = 0; row < height; ++row)
        {
            const int inset =
                row >= height - radiusUnits
                    ? computeRoundedRectRowInset(height - 1 - row,
                                                 radiusUnits * 2, radiusUnits)
                    : 0;
            const int rowX = x + inset;
            const int rowWidth = width - inset * 2;
            if (rowWidth <= 0)
            {
                continue;
            }

            const SDL_FRect rowRect{
                fromRoundedRectGridUnits(rowX, pixelScale),
                fromRoundedRectGridUnits(y + row, pixelScale),
                fromRoundedRectGridUnits(rowWidth, pixelScale),
                fromRoundedRectGridUnits(1, pixelScale)};
            SDL_RenderFillRect(renderer, &rowRect);
        }
    }

    inline void insetRoundedRect(SDL_FRect &rect, float &radius,
                                 const float inset,
                                 const uint8_t pixelScale)
    {
        if (inset <= 0.0f)
        {
            return;
        }

        rect.x += inset;
        rect.y += inset;
        rect.w = std::max(0.0f, rect.w - inset * 2.0f);
        rect.h = std::max(0.0f, rect.h - inset * 2.0f);
        rect = snapRoundedRectToPixelGrid(rect, pixelScale);
        radius = snapRoundedRectRadiusToPixelGrid(
            std::max(0.0f, radius - inset), pixelScale);
    }

    inline void drawRoundedRectBordered(SDL_Renderer *renderer,
                                        const SDL_FRect &rect, float radius,
                                        const SDL_Color &borderColor,
                                        const SDL_Color &fillColor,
                                        const uint8_t pixelScale,
                                        const float borderThickness = 1.0f)
    {
        drawRoundedRectPixelPerfect(renderer, rect, radius, fillColor,
                                    pixelScale);
        drawRoundedRectBorderPixelPerfect(renderer, rect, radius, borderColor,
                                          pixelScale, borderThickness, true,
                                          true);
    }

    inline void drawTopRoundedRectBordered(SDL_Renderer *renderer,
                                           const SDL_FRect &rect, float radius,
                                           const SDL_Color &borderColor,
                                           const SDL_Color &fillColor,
                                           const uint8_t pixelScale,
                                           const float borderThickness = 1.0f)
    {
        drawTopRoundedRectPixelPerfect(renderer, rect, radius, fillColor,
                                       pixelScale);
        drawRoundedRectBorderPixelPerfect(renderer, rect, radius, borderColor,
                                          pixelScale, borderThickness, true,
                                          false);
    }

    inline void drawBottomRoundedRectBordered(SDL_Renderer *renderer,
                                              const SDL_FRect &rect,
                                              float radius,
                                              const SDL_Color &borderColor,
                                              const SDL_Color &fillColor,
                                              const uint8_t pixelScale,
                                              const float borderThickness =
                                                  1.0f)
    {
        drawBottomRoundedRectPixelPerfect(renderer, rect, radius, fillColor,
                                          pixelScale);
        drawRoundedRectBorderPixelPerfect(renderer, rect, radius, borderColor,
                                          pixelScale, borderThickness, false,
                                          true);
    }

    inline void drawVerticalBorderedRect(SDL_Renderer *renderer,
                                         const SDL_FRect &rect,
                                         const SDL_Color &borderColor,
                                         const SDL_Color &fillColor,
                                         const uint8_t pixelScale,
                                         const float borderThickness = 1.0f)
    {
        drawRoundedRectPixelPerfect(renderer, rect, 0.0f, fillColor,
                                    pixelScale);
        drawRoundedRectBorderPixelPerfect(renderer, rect, 0.0f, borderColor,
                                          pixelScale, borderThickness, false,
                                          false);
    }


    // Internal: quarter filled arc
    inline void drawQuarterFilled(SDL_Renderer *renderer, const float cx,
                                  const float cy, const float radius,
                                  const float startAngleDeg,
                                  const SDL_Color &col)
    {
        constexpr int segments = 16;
        std::vector<SDL_Vertex> verts;
        std::vector<int> indices;

        // Center vertex
        SDL_Vertex center{};
        center.position.x = cx;
        center.position.y = cy;
        const SDL_FColor fcol{col.r / 255.f, col.g / 255.f, col.b / 255.f,
                              col.a / 255.f};
        center.color = fcol;
        center.tex_coord.x = 0.0f;
        center.tex_coord.y = 0.0f;
        verts.push_back(center);

        // Arc vertices
        for (int i = 0; i <= segments; ++i)
        {
            const float theta =
                (startAngleDeg + 90.0f * i / segments) * (float)M_PI / 180.0f;
            const float x = cx + std::cos(theta) * radius;
            const float y = cy + std::sin(theta) * radius;

            SDL_Vertex v{};
            v.position.x = x;
            v.position.y = y;
            v.color = fcol;
            v.tex_coord.x = 0.0f;
            v.tex_coord.y = 0.0f;
            verts.push_back(v);

            if (i > 0)
            {
                indices.push_back(0);
                indices.push_back(i);
                indices.push_back(i + 1);
            }
        }

        SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(),
                           indices.data(), (int)indices.size());
    }

    // Internal: quarter outline arc
    inline void drawQuarterOutline(SDL_Renderer *renderer, const float cx,
                                   const float cy, const float radius,
                                   const float startAngleDeg,
                                   const SDL_Color &col)
    {
        constexpr int segments = 24;
        constexpr float step = 90.0f / segments;

        float prevX = cx + std::cos(startAngleDeg * M_PI / 180.0f) * radius;
        float prevY = cy + std::sin(startAngleDeg * M_PI / 180.0f) * radius;

        for (int i = 1; i <= segments; ++i)
        {
            const float theta =
                (startAngleDeg + step * i) * (float)M_PI / 180.0f;
            const float x = cx + std::cos(theta) * radius;
            const float y = cy + std::sin(theta) * radius;

            SDL_RenderLine(renderer, prevX, prevY, x, y);

            prevX = x;
            prevY = y;
        }
    }

    // Public: filled rounded rect
    inline void drawRoundedRect(SDL_Renderer *renderer, const SDL_FRect &rect,
                                float radius, const SDL_Color &col)
    {
        if (radius <= 0.0f)
        {
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(renderer, &rect);
            return;
        }

        radius = clampRoundedRectRadius(rect, radius);

        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        // Core rectangles
        const SDL_FRect core = planRoundedRectCore(rect, radius);
        SDL_RenderFillRect(renderer, &core);

        const SDL_FRect vertical = planRoundedRectVerticalCore(rect, radius);
        SDL_RenderFillRect(renderer, &vertical);

        // Four corners
        drawQuarterFilled(renderer, rect.x + radius, rect.y + radius, radius,
                          180.0f, col); // TL
        drawQuarterFilled(renderer, rect.x + rect.w - radius, rect.y + radius,
                          radius, 270.0f, col); // TR
        drawQuarterFilled(renderer, rect.x + rect.w - radius,
                          rect.y + rect.h - radius, radius, 0.0f, col); // BR
        drawQuarterFilled(renderer, rect.x + radius, rect.y + rect.h - radius,
                          radius, 90.0f, col); // BL
    }

    inline void drawRoundedRectOutline(SDL_Renderer *renderer,
                                       const SDL_FRect &rect, float radius,
                                       const SDL_Color &col)
    {
        if (radius <= 0.0f)
        {
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            // Subtract 1 from width/height to stay inside rect
            const SDL_FRect r = {rect.x, rect.y, rect.w - 1.0f, rect.h - 1.0f};
            SDL_RenderRect(renderer, &r);
            return;
        }

        const auto geometry = planRoundedRectGeometry(rect, radius);
        radius = geometry.radius;

        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        const float x0 = geometry.x0;
        const float y0 = geometry.y0;
        const float x1 = geometry.x1;
        const float y1 = geometry.y1;

        // Straight edges
        SDL_RenderLine(renderer, x0 + radius, y0, x1 - radius, y0); // top
        SDL_RenderLine(renderer, x1, y0 + radius, x1, y1 - radius); // right
        SDL_RenderLine(renderer, x1 - radius, y1, x0 + radius, y1); // bottom
        SDL_RenderLine(renderer, x0, y1 - radius, x0, y0 + radius); // left

        // Rounded corners
        drawQuarterOutline(renderer, x0 + radius, y0 + radius, radius, 180.0f,
                           col); // TL
        drawQuarterOutline(renderer, x1 - radius, y0 + radius, radius, 270.0f,
                           col); // TR
        drawQuarterOutline(renderer, x1 - radius, y1 - radius, radius, 0.0f,
                           col); // BR
        drawQuarterOutline(renderer, x0 + radius, y1 - radius, radius, 90.0f,
                           col); // BL
    }

    // Top corners only
    inline void drawTopRoundedRect(SDL_Renderer *renderer,
                                   const SDL_FRect &rect, float radius,
                                   const SDL_Color &col)
    {
        if (radius <= 0.0f)
        {
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(renderer, &rect);
            return;
        }

        radius = clampRoundedRectRadius(rect, radius);
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        // Core rectangle covering bottom and middle
        const SDL_FRect core = {rect.x, rect.y + radius, rect.w, rect.h - radius};
        SDL_RenderFillRect(renderer, &core);

        // Top horizontal bar (between corners)
        const SDL_FRect topBar = {rect.x + radius, rect.y, rect.w - 2 * radius,
                                  radius};
        SDL_RenderFillRect(renderer, &topBar);

        // Top corners
        drawQuarterFilled(renderer, rect.x + radius, rect.y + radius, radius,
                          180.0f, col); // TL
        drawQuarterFilled(renderer, rect.x + rect.w - radius, rect.y + radius,
                          radius, 270.0f, col); // TR
    }

    // Bottom corners only
    inline void drawBottomRoundedRect(SDL_Renderer *renderer,
                                      const SDL_FRect &rect, float radius,
                                      const SDL_Color &col)
    {
        if (radius <= 0.0f)
        {
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(renderer, &rect);
            return;
        }

        radius = clampRoundedRectRadius(rect, radius);
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        // Core rectangle covering top and middle
        const SDL_FRect core = {rect.x, rect.y, rect.w, rect.h - radius};
        SDL_RenderFillRect(renderer, &core);

        // Bottom horizontal bar (between corners)
        const SDL_FRect bottomBar = {rect.x + radius, rect.y + rect.h - radius,
                                     rect.w - 2 * radius, radius};
        SDL_RenderFillRect(renderer, &bottomBar);

        // Bottom corners
        drawQuarterFilled(renderer, rect.x + rect.w - radius,
                          rect.y + rect.h - radius, radius, 0.0f, col); // BR
        drawQuarterFilled(renderer, rect.x + radius, rect.y + rect.h - radius,
                          radius, 90.0f, col); // BL
    }

    // Top corners only outline (no bottom edge)
    inline void drawTopRoundedRectOutline(SDL_Renderer *renderer,
                                          const SDL_FRect &rect, float radius,
                                          const SDL_Color &col)
    {
        if (radius <= 0.0f)
        {
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_RenderLine(renderer, rect.x, rect.y, rect.x + rect.w - 1.0f,
                           rect.y); // top
            SDL_RenderLine(renderer, rect.x, rect.y, rect.x,
                           rect.y + rect.h - 1.0f); // left
            SDL_RenderLine(renderer, rect.x + rect.w - 1.0f, rect.y,
                           rect.x + rect.w - 1.0f,
                           rect.y + rect.h - 1.0f); // right
            return;
        }

        const auto geometry = planRoundedRectGeometry(rect, radius);
        radius = geometry.radius;
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        const float x0 = geometry.x0;
        const float y0 = geometry.y0;
        const float x1 = geometry.x1;
        const float y1 = geometry.y1;

        // Top edge
        SDL_RenderLine(renderer, x0 + radius, y0, x1 - radius, y0);

        // Vertical edges
        SDL_RenderLine(renderer, x0, y0 + radius, x0, y1);
        SDL_RenderLine(renderer, x1, y0 + radius, x1, y1);

        // Top corners
        drawQuarterOutline(renderer, x0 + radius, y0 + radius, radius, 180.0f,
                           col); // TL
        drawQuarterOutline(renderer, x1 - radius, y0 + radius, radius, 270.0f,
                           col); // TR
    }

    // Bottom corners only outline (no top edge)
    inline void drawBottomRoundedRectOutline(SDL_Renderer *renderer,
                                             const SDL_FRect &rect,
                                             float radius, const SDL_Color &col)
    {
        if (radius <= 0.0f)
        {
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_RenderLine(renderer, rect.x, rect.y + rect.h - 1.0f,
                           rect.x + rect.w - 1.0f,
                           rect.y + rect.h - 1.0f); // bottom
            SDL_RenderLine(renderer, rect.x, rect.y, rect.x,
                           rect.y + rect.h - 1.0f); // left
            SDL_RenderLine(renderer, rect.x + rect.w - 1.0f, rect.y,
                           rect.x + rect.w - 1.0f,
                           rect.y + rect.h - 1.0f); // right
            return;
        }

        const auto geometry = planRoundedRectGeometry(rect, radius);
        radius = geometry.radius;
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);

        const float x0 = geometry.x0;
        const float y0 = geometry.y0;
        const float x1 = geometry.x1;
        const float y1 = geometry.y1;

        // Bottom edge
        SDL_RenderLine(renderer, x0 + radius, y1, x1 - radius, y1);

        // Vertical edges
        SDL_RenderLine(renderer, x0, y0, x0, y1 - radius);
        SDL_RenderLine(renderer, x1, y0, x1, y1 - radius);

        // Bottom corners
        drawQuarterOutline(renderer, x1 - radius, y1 - radius, radius, 0.0f,
                           col); // BR
        drawQuarterOutline(renderer, x0 + radius, y1 - radius, radius, 90.0f,
                           col); // BL
    }

    // Left and right edges only (no top/bottom edges)
    inline void drawVerticalEdges(SDL_Renderer *renderer, const SDL_FRect &rect,
                                  const SDL_Color &col)
    {
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        SDL_RenderLine(renderer, rect.x, rect.y, rect.x,
                       rect.y + rect.h - 1.0f); // left
        SDL_RenderLine(renderer, rect.x + rect.w - 1.0f, rect.y,
                       rect.x + rect.w - 1.0f, rect.y + rect.h - 1.0f); // right
    }
} // namespace cupuacu::gui
