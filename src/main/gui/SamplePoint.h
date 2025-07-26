#pragma once

#include "Component.h"

#include "../CupuacuState.h"

class SamplePoint : public Component {

private:
    const uint64_t sampleIndex;
    bool isDragging = false;
    float prevY = 0.f;
    float dragYPos = 0.f;

    int16_t getSampleValueForYPos(const int16_t y, const uint16_t h, const double v)
    {
        return ((h/2.f - y) * 32768) / (v * (h/2.f));
    }

public:
    SamplePoint(CupuacuState *state, const uint64_t sampleIndexToUse) :
        Component(state), sampleIndex(sampleIndexToUse)
    {}

    void mouseEnter() override
    {
        setDirty();
    }

    void mouseLeave() override
    {
        setDirty();
    }

    bool onHandleEvent(const SDL_Event &event) override
    {
        switch (event.type)
        {
            case SDL_EVENT_MOUSE_MOTION:
            {
                if (!isDragging) return false;

                dragYPos += event.motion.yrel;
                setYPos(dragYPos);
                const auto vertCenter = getYPos() + (getHeight() * 0.5f);
                const auto newSampleValue = getSampleValueForYPos(vertCenter, getParent()->getHeight(), state->verticalZoom);
                state->sampleDataL[sampleIndex] = newSampleValue;
                getParent()->setDirtyRecursive();
                return true;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                if (event.button.x < 0 ||
                    event.button.x > getWidth() ||
                    event.button.y < 0 ||
                    event.button.y > getHeight())
                {
                    return false;
                }

                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    isDragging = true;
                    prevY = 0.f;
                    dragYPos = getYPos();
                    return true;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                if (event.button.button == SDL_BUTTON_LEFT && isDragging)
                {
                    isDragging = false;
                    setDirty();
                    return true;
                }
                break;
            }
            default:
                break;
        }

        return false;
    }

    void onDraw(SDL_Renderer *r) override
    {
        SDL_SetRenderDrawColor(r, 0, (isMouseOver()|| isDragging) ? 255 : 185, 0, 255);
        SDL_FRect rectToFill {0, 0, (float)getWidth(), (float)getHeight()};
        SDL_RenderFillRect(r, &rectToFill);
    }
};

