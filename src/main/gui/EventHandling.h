#pragma once
#include <SDL3/SDL.h>
#include "../CupuacuState.h"
#include "Gui.h"
#include "Component.h"
#include "Waveform.h"
#include "Waveforms.h"
#include "WaveformsUnderlay.h"
#include "keyboard_handling.h"

static bool wasMaximized = false;

static SDL_Cursor* defaultCursor = nullptr;
static SDL_Cursor* textCursor = nullptr;
static SDL_Cursor* pointerCursor = nullptr;
static SDL_Cursor* currentCursor = nullptr;

static void initCursors() {
    defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    textCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    pointerCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    currentCursor = defaultCursor;
    SDL_SetCursor(currentCursor);
}

static void cleanupCursors() {
    if (defaultCursor) SDL_DestroyCursor(defaultCursor);
    if (textCursor) SDL_DestroyCursor(textCursor);
    if (pointerCursor) SDL_DestroyCursor(pointerCursor);
}

static void updateMousePointer(const Component* underMouse) {
    SDL_Cursor* newCursor = defaultCursor;

    if (dynamic_cast<const Waveform*>(underMouse) ||
        dynamic_cast<const Waveforms*>(underMouse) ||
        dynamic_cast<const WaveformsUnderlay*>(underMouse)) {
        newCursor = textCursor;
    } else if (dynamic_cast<const SamplePoint*>(underMouse)) {
        newCursor = pointerCursor;
    }

    if (newCursor != currentCursor) {
        SDL_SetCursor(nullptr);
        SDL_SetCursor(newCursor);
        currentCursor = newCursor;
    }
}

static void updateComponentUnderMouse(CupuacuState *state, const int32_t mouseX, const int32_t mouseY, bool &componentUnderMouseChanged)
{
    auto oldComponentUnderMouse = state->componentUnderMouse;
    const auto newComponentUnderMouse = state->rootComponent->findComponentAt(mouseX, mouseY);

    if (state->componentUnderMouse != newComponentUnderMouse)
    {
        state->componentUnderMouse = newComponentUnderMouse;
        componentUnderMouseChanged = true;

        if (oldComponentUnderMouse != nullptr)
        {
            oldComponentUnderMouse->mouseLeave();
        }

        if (newComponentUnderMouse != nullptr)
        {
            newComponentUnderMouse->mouseEnter();
        }
    }
}

static void scaleMouseCoordinates(const CupuacuState *const state, float &x, float &y)
{
    SDL_FPoint canvasDimensions;
    SDL_GetTextureSize(state->canvas, &canvasDimensions.x, &canvasDimensions.y);

    SDL_Point winDimensions;
    SDL_GetWindowSize(state->window, &winDimensions.x, &winDimensions.y);

    x *= canvasDimensions.x / winDimensions.x;
    y *= canvasDimensions.y / winDimensions.y;
}

static void scaleMouseButtonEvent(const CupuacuState *const state, SDL_Event *e)
{
    scaleMouseCoordinates(state, e->button.x, e->button.y);
}

static void scaleMouseMotionEvent(const CupuacuState *const state, SDL_Event *e)
{
    scaleMouseCoordinates(state, e->motion.x, e->motion.y);
    scaleMouseCoordinates(state, e->motion.xrel, e->motion.yrel);
}

inline SDL_AppResult handleAppEvent(CupuacuState *state, SDL_Event *event)
{
    bool componentUnderMouseChanged = false;

    switch (event->type)
    {
        case SDL_EVENT_QUIT:
            cleanupCursors();
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            wasMaximized = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            {
                int winW, winH;
                SDL_GetWindowSize(state->window, &winW, &winH);

                int hpp = state->pixelScale;

                int newW = (winW / hpp) * hpp;
                int newH = (winH / hpp) * hpp;

                if (newW != winW || newH != winH)
                {
                    if (wasMaximized)
                    {
                        wasMaximized = false;
                        SDL_RestoreWindow(state->window);
                        SDL_SetWindowSize(state->window, newW, newH);
                        SDL_SetWindowPosition(state->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                    }
                    else
                    {
                        SDL_SetWindowSize(state->window, newW, newH);
                    }
                    break;
                }

                resizeComponents(state);
                break;
            }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            {
                if (state->capturingComponent == nullptr && !state->selection.isActive())
                {
                    for (auto* waveform : state->waveforms)
                    {
                        waveform->clearHighlight();
                    }
                    state->componentUnderMouse = nullptr;
                    componentUnderMouseChanged = true;
                }
                break;
            }
        case SDL_EVENT_KEY_DOWN:
            handleKeyDown(event, state);
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                if (event->type == SDL_EVENT_MOUSE_MOTION)
                {
                    scaleMouseMotionEvent(state, event);
                }
                else
                {
                    scaleMouseButtonEvent(state, event);
                }

                if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
                {
                    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
                    {
                        updateComponentUnderMouse(state, event->button.x, event->button.y, componentUnderMouseChanged);
                    }

                    if (state->capturingComponent != nullptr)
                    {
                        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
                        {
                            if (!state->capturingComponent->constainsAbsoluteCoordinate(event->button.x, event->button.y))
                            {
                                state->capturingComponent->mouseLeave();
                            }
                        }

                        state->capturingComponent->handleEvent(*event);
                        
                        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
                        {
                            state->capturingComponent = nullptr;
                        }
                        break;
                    }
                }

                state->rootComponent->handleEvent(*event);

                if (event->type == SDL_EVENT_MOUSE_MOTION && state->capturingComponent == nullptr)
                {
                    updateComponentUnderMouse(state, event->motion.x, event->motion.y, componentUnderMouseChanged);
                }
            }
            break;
    }

    if (componentUnderMouseChanged)
    {
        updateMousePointer(state->componentUnderMouse);
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        state->selection.printInfo();
    }

    return SDL_APP_CONTINUE;
}
