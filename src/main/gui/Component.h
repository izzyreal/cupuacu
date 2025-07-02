#pragma once
#include <SDL3/SDL.h>

#include <vector>

struct Component {
    SDL_Rect rect;  // position and size
    std::vector<std::unique_ptr<Component>> children;
    int zIndex = 0;

    // Will be called every frame
    virtual void timerCallback() { for (auto &c : children) { c->timerCallback(); }}

    void draw(SDL_Renderer* renderer)
    {
        SDL_SetRenderViewport(renderer, &rect);
        SDL_Rect localClip = {0, 0, rect.w, rect.h};
        SDL_SetRenderClipRect(renderer, &localClip);

        onDraw(renderer);

        std::sort(children.begin(), children.end(), [](auto& a, auto& b) {
            return a->zIndex < b->zIndex;
        });
        for (auto& c : children)
            c->draw(renderer);

        SDL_SetRenderViewport(renderer, nullptr);
        SDL_SetRenderClipRect(renderer, nullptr);
    }

    virtual void onDraw(SDL_Renderer* renderer)
    {
        // Base: maybe draw background or border
    }

    bool handleEvent(const SDL_Event& e)
    {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_MOUSE_MOTION)
        {
            SDL_Event e_rel = e;

            if (e.type == SDL_EVENT_MOUSE_MOTION)
            {
                e_rel.motion.x -= rect.x;
                e_rel.motion.y -= rect.y;
            }
            else
            {
                e_rel.button.x -= rect.x;
                e_rel.button.y -= rect.y;
            }

            const int x = e_rel.type == SDL_EVENT_MOUSE_MOTION ? e_rel.motion.x : e_rel.button.x;
            const int y = e_rel.type == SDL_EVENT_MOUSE_MOTION ? e_rel.motion.y : e_rel.button.y;

            std::sort(children.begin(), children.end(), [](auto& a, auto& b) {
                return a->zIndex > b->zIndex;
            });

            for (auto& c : children)
            {
                if (c->handleEvent(e_rel))
                {
                    return true;
                }
            }

            return onHandleEvent(e_rel);
        }

        return false;
    }

    virtual bool onHandleEvent(const SDL_Event& e) {
        return false; // override in subclasses
    }
};
