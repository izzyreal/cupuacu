#pragma once
#include <SDL3/SDL.h>

#include <vector>
#include <memory>
#include <algorithm>
#include <cstdint>

#include "../CupuacuState.h"

class Component {
private:
    // Optional name, useful for debugging
    std::string componentName;

    bool mouseIsOver = false;
    bool dirty = false;

    // Position relative to parent
    uint16_t xPos = 0, yPos = 0;

    uint16_t width = 0, height = 0;
    
    Component *parent = nullptr;
    std::vector<std::unique_ptr<Component>> children;

    void setParent(Component *parentToUse)
    {
        parent = parentToUse;
    }

protected:
    CupuacuState *state;

    void removeAllChildren()
    {
        children.clear();
        setDirty();
    }

    const bool isMouseOver()
    {
        return mouseIsOver;
    }

    Component* getParent()
    {
        return parent;
    }

public:
    Component(CupuacuState *stateToUse) :
        state(stateToUse)
    {
    }
    
    Component(CupuacuState *stateToUse, const std::string componentNameToUse) :
        state(stateToUse), componentName(componentNameToUse)
    {
    }

    /**
     * Note that this invalidates the unique_ptr.
     */
    template <typename T>
    T* addChildAndSetDirty(std::unique_ptr<T> &childToAdd)
    {
        children.push_back(std::move(childToAdd));
        children.back()->setParent(this);
        children.back()->setDirty();
        return dynamic_cast<T*>(children.back().get());
    }

    void setBounds(const uint16_t xPosToUse,
                   const uint16_t yPosToUse,
                   const uint16_t widthToUse,
                   const uint16_t heightToUse)
    {
        xPos = xPosToUse;
        yPos = yPosToUse;
        width = widthToUse;
        height = heightToUse;
        setDirty();
    }

    void setSize(const uint16_t widthToUse,
                 const uint16_t heightToUse)
    {
        width = widthToUse;
        height = heightToUse;
        setDirty();
    }

    void setYPos(const uint16_t yPosToUse)
    {
        yPos = yPosToUse;
        setDirty();
    }

    void setDirty()
    {
        dirty = true;
    }

    void setDirtyRecursive()
    {
        dirty = true;
        for (auto &c : children) c->setDirtyRecursive();
    }

    bool isDirtyRecursive()
    {
        if (dirty)
        {
            return true;
        }

        for (auto &c : children)
        {
            if (c->isDirtyRecursive())
            {
                return true;
            }
        }

        return false;
    }

    void draw(SDL_Renderer* renderer)
    {
        SDL_Rect viewPortRect;
        SDL_GetRenderViewport(renderer, &viewPortRect);
        SDL_Rect oldViewPortRect = viewPortRect;
        viewPortRect.x += xPos;
        viewPortRect.y += yPos;
        SDL_SetRenderViewport(renderer, &viewPortRect);
        SDL_Rect localClip = {0, 0, width, height};
        SDL_SetRenderClipRect(renderer, &localClip);

        if (dirty)
        {
            onDraw(renderer);
            dirty = false;
        }

        for (auto& c : children)
        {
            c->draw(renderer);
        }

        SDL_SetRenderViewport(renderer, &oldViewPortRect);
        SDL_SetRenderClipRect(renderer, nullptr);
    }

    virtual void onDraw(SDL_Renderer* renderer) {}
    virtual void mouseLeave() {}
    virtual void mouseEnter() {}
    virtual bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) { return false; }
    virtual bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) { return false; }
    virtual bool mouseMove(const int32_t mouseX,
                           const int32_t mouseY,
                           const float mouseRelY, // This should be an int as well, but it should be accumulated by the system.
                                                  // For now, components are expected to accumulate.
                           const bool leftButtonIsDown)
    {
        return false;
    }

    bool handleEvent(const SDL_Event& e)
    {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
            e.type == SDL_EVENT_MOUSE_MOTION)
        {
            SDL_Event e_rel = e;

            if (e_rel.type == SDL_EVENT_MOUSE_MOTION)
            {
                e_rel.motion.x -= xPos;
                e_rel.motion.y -= yPos;
            }
            else
            {
                e_rel.button.x -= xPos;
                e_rel.button.y -= yPos;
            }

            for (auto& c : children)
            {
                if (c->handleEvent(e_rel))
                {
                    return true;
                }
            }

            const int x = e_rel.type == SDL_EVENT_MOUSE_MOTION ? e_rel.motion.x : e_rel.button.x;
            const int y = e_rel.type == SDL_EVENT_MOUSE_MOTION ? e_rel.motion.y : e_rel.button.y;

            Component *capturingComponent = state->capturingComponent;

            if (x < 0 || x > width || y < 0 || y > height)
            {
                if (e_rel.type == SDL_EVENT_MOUSE_MOTION)
                {
                    if (mouseIsOver)
                    {
                        mouseIsOver = false;
                        mouseLeave();
                    }

                    if (this == capturingComponent)
                    {
                        if (mouseMove(e_rel.motion.x, e_rel.motion.y, e_rel.motion.yrel, e_rel.motion.state & SDL_BUTTON_LMASK))
                        {
                            return true;
                        }
                    }
                }
                else if (e_rel.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                         e_rel.button.button == SDL_BUTTON_LEFT)
                {
                    if (this == capturingComponent)
                    {
                        if (mouseLeftButtonUp(e_rel.button.clicks, e_rel.button.x, e_rel.button.y))
                        {
                            state->capturingComponent = nullptr;
                            return true;
                        }
                    }
                }
            }
            else
            {
                if (e_rel.type == SDL_EVENT_MOUSE_MOTION)
                {
                    if (!mouseIsOver)
                    {
                        mouseIsOver = true;
                        mouseEnter();
                    }

                    if (mouseMove(e_rel.motion.x, e_rel.motion.y, e_rel.motion.yrel, e_rel.motion.state & SDL_BUTTON_LMASK))
                    {
                        return true;
                    }
                }
                else if (e_rel.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                         e_rel.button.button == SDL_BUTTON_LEFT)
                {
                    if (mouseLeftButtonDown(e_rel.button.clicks, e_rel.button.x, e_rel.button.y))
                    {
                        state->capturingComponent = this;
                        return true;
                    }
                }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                         e_rel.button.button == SDL_BUTTON_LEFT)
                {
                    if (mouseLeftButtonUp(e_rel.button.clicks, e_rel.button.x, e_rel.button.y))
                    {
                        state->capturingComponent = nullptr;
                        return true;
                    }
                }
            }

            return onHandleEvent(e_rel);
        }

        return false;
    }

    virtual bool onHandleEvent(const SDL_Event& e) { return false; }

    uint16_t getWidth() { return width; }
    uint16_t getHeight() { return height; }
    uint16_t getXPos() { return xPos; }
    uint16_t getYPos() { return yPos; }

    // Called every frame
    virtual void timerCallback() { for (auto &c : children) { c->timerCallback(); }}
};
