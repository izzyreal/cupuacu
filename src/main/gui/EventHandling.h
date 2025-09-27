#pragma once
#include <SDL3/SDL.h>
#include "../CupuacuState.h"
#include "Gui.h"
#include "Component.h"
#include "keyboard_handling.h"

static bool wasMaximized = false;

inline SDL_AppResult handleAppEvent(CupuacuState *state, SDL_Event *event)
{
    switch (event->type)
    {
        case SDL_EVENT_QUIT:
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
                }
                break;
            }
        case SDL_EVENT_KEY_DOWN:
            handleKeyDown(event, state);
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            {
                SDL_FPoint canvasDimensions;
                SDL_GetTextureSize(state->canvas, &canvasDimensions.x, &canvasDimensions.y);

                SDL_Point winDimensions;
                SDL_GetWindowSize(state->window, &winDimensions.x, &winDimensions.y);

                SDL_Event e = *event;
                
                if (e.type == SDL_EVENT_MOUSE_MOTION)
                {
                    e.motion.x *= canvasDimensions.x / winDimensions.x;
                    e.motion.xrel *= canvasDimensions.x / winDimensions.x;
                    e.motion.y *= canvasDimensions.y / winDimensions.y;
                    e.motion.yrel *= canvasDimensions.y / winDimensions.y;
                }
                else
                {
                    e.button.x *= (float)canvasDimensions.x / winDimensions.x;
                    e.button.y *= (float)canvasDimensions.y / winDimensions.y;
                }

                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                {
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                    {
                        auto oldComponentUnderMouse = state->componentUnderMouse;
                        const auto newComponentUnderMouse = state->rootComponent->findComponentAt(e.motion.x, e.motion.y);

                        if (state->componentUnderMouse != newComponentUnderMouse)
                        {
                            state->componentUnderMouse = newComponentUnderMouse;

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

                    if (state->capturingComponent != nullptr)
                    {
                        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                        {
                            if (!state->capturingComponent->constainsAbsoluteCoordinate(e.button.x, e.button.y))
                            {
                                state->capturingComponent->mouseLeave();
                            }
                        }

                        state->capturingComponent->handleEvent(e);
                        
                        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                        {
                            state->capturingComponent = nullptr;
                        }
                        break;
                    }
                }

                state->rootComponent->handleEvent(e);

                if (e.type == SDL_EVENT_MOUSE_MOTION && state->capturingComponent == nullptr)
                {
                    auto oldComponentUnderMouse = state->componentUnderMouse;
                    const auto newComponentUnderMouse = state->rootComponent->findComponentAt(e.motion.x, e.motion.y);

                    if (state->componentUnderMouse != newComponentUnderMouse)
                    {
                        state->componentUnderMouse = newComponentUnderMouse;

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
            }
            break;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        //state->rootComponent->printTree();
        state->selection.printInfo();
    }

    return SDL_APP_CONTINUE;
}

