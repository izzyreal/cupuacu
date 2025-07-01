#pragma once
#include <SDL3/SDL.h>

#include <vector>

struct Component {
    SDL_Rect rect;  // position and size
    std::vector<std::unique_ptr<Component>> children;
    int zIndex = 0;

    virtual void draw(SDL_Renderer* renderer) {
        // Set clip rect to this component's rect
        SDL_SetRenderClipRect(renderer, &rect);

        // Draw self (override in subclasses)
        onDraw(renderer);

        // Draw children in z-order
        std::sort(children.begin(), children.end(), [](auto& a, auto& b) {
            return a->zIndex < b->zIndex;
        });
        for (auto& c : children)
            c->draw(renderer);

        SDL_SetRenderClipRect(renderer, nullptr); // reset clipping
    }

    virtual void onDraw(SDL_Renderer* renderer) {
        // Base: maybe draw background or border
    }

    virtual bool handleEvent(const SDL_Event& e) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            int x = e.button.x, y = e.button.y;
            SDL_Point p{x,y};

            if (!SDL_PointInRect(&p, &rect))
                return false;

            // Dispatch to children front-to-back (highest z first)
            std::sort(children.begin(), children.end(), [](auto& a, auto& b) {
                return a->zIndex > b->zIndex;
            });
            for (auto& c : children) {
                if (c->handleEvent(e))
                    return true;
            }
            // Handle self event here if needed
            return onHandleEvent(e);
        }
        return false;
    }

    virtual bool onHandleEvent(const SDL_Event& e) {
        return false; // override in subclasses
    }
};
