#pragma once

#include <SDL3/SDL.h>
#include <cassert>
#include <stdexcept>

struct Helpers
{
    static bool intersects(const SDL_Rect r1, const SDL_Rect r2)
    {
        SDL_Rect result;
        return SDL_GetRectIntersection(&r1, &r2, &result);
    }

    static void printRect(const SDL_Rect r)
    {
        printf("x: %i, y: %i, w: %i, h: %i\n", r.x, r.y, r.w, r.h);
    }

    static SDL_FRect rectToFRect(const SDL_Rect r)
    {
        return SDL_FRect{(float)r.x, (float)r.y, (float)r.w, (float)r.h};
    }

    static void setRenderDrawColor(SDL_Renderer *r, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    }

    static void fillRect(SDL_Renderer *r, SDL_FRect rect, SDL_Color c)
    {
        setRenderDrawColor(r, c);
        SDL_RenderFillRect(r, &rect);
    }

    static void fillRect(SDL_Renderer *r, SDL_Rect rect, SDL_Color c)
    {
        setRenderDrawColor(r, c);
        const SDL_FRect rectToFill(rectToFRect(rect));
        SDL_RenderFillRect(r, &rectToFill);
    }

    static SDL_Rect subtractRect(const SDL_Rect &rect, const SDL_Rect &clip)
    {
        if (!((clip.h == rect.h && clip.w <= rect.w) ||
              (clip.w == rect.w && clip.h <= rect.h)))
        {
            return rect;
        }

        SDL_Rect result = rect;

        if (!SDL_HasRectIntersection(&rect, &clip))
        {
            throw std::invalid_argument("Rects must intersect");
        }

        if (clip.x <= rect.x && clip.x + clip.w >= rect.x + rect.w)
        {
            result.w = 0;
        }
        else if (clip.x <= rect.x)
        {
            const int overlap = (clip.x + clip.w) - rect.x;
            result.x += overlap;
            result.w -= overlap;
        }
        else if (clip.x + clip.w >= rect.x + rect.w)
        {
            const int overlap = rect.x + rect.w - clip.x;
            result.w -= overlap;
        }
        else
        {
            result.w = clip.x - rect.x;
        }

        return result;
    }
};
