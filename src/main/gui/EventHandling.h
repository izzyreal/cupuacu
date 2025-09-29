#pragma once
#include <SDL3/SDL.h>
#include "../CupuacuState.h"
#include "Gui.h"
#include "Component.h"
#include "Waveform.h"
#include "TriangleMarker.h"
#include "WaveformsUnderlay.h"
#include "keyboard_handling.h"
#include "../ResourceUtil.hpp"

static bool wasMaximized = false;

static SDL_Cursor* defaultCursor = nullptr;
static SDL_Cursor* textCursor = nullptr;
static SDL_Cursor* pointerCursor = nullptr;
static SDL_Cursor* currentCursor = nullptr;
static SDL_Cursor* selectLCursor = nullptr;
static SDL_Cursor* selectRCursor = nullptr;

static SDL_Cursor* loadCustomCursor(const std::string& filename, int hot_x, int hot_y)
{
    const auto data = get_resource_data(filename);
    if (data.empty()) {
        SDL_Log("Cursor resource '%s' not found", filename.c_str());
        return nullptr;
    }

    SDL_IOStream* io = SDL_IOFromConstMem(data.data(), (int)data.size());
    SDL_Surface* surface = SDL_LoadBMP_IO(io, 1);
    if (!surface) {
        SDL_Log("SDL_LoadBMP_IO failed: %s", SDL_GetError());
        return nullptr;
    }

    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hot_x, hot_y);
    SDL_DestroySurface(surface);

    if (!cursor) {
        SDL_Log("SDL_CreateColorCursor failed: %s", SDL_GetError());
    }
    return cursor;
}

static void initCursors()
{
    defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    textCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    pointerCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    currentCursor = defaultCursor;

    selectLCursor = loadCustomCursor("select_l.bmp", 0, 0);
    selectRCursor = loadCustomCursor("select_r.bmp", 0, 0);
    
    SDL_SetCursor(currentCursor);
}

static void cleanupCursors()
{
    if (defaultCursor) SDL_DestroyCursor(defaultCursor);
    if (textCursor) SDL_DestroyCursor(textCursor);
    if (pointerCursor) SDL_DestroyCursor(pointerCursor);
}

static void updateMouseCursor(const CupuacuState *state)
{
    SDL_Cursor* newCursor = defaultCursor;

    if (dynamic_cast<const Waveform*>(state->componentUnderMouse))
    {
        if (state->selectedChannels == SelectedChannels::LEFT)
        {
                newCursor = selectLCursor;
        }
        else if (state->selectedChannels == SelectedChannels::RIGHT)
        {
                newCursor = selectRCursor;
        }
        else /* SelectedChannels::BOTH */
        {
            newCursor = textCursor;
        }
    }
    else if (dynamic_cast<const SamplePoint*>(state->componentUnderMouse) ||
             dynamic_cast<const TriangleMarker*>(state->componentUnderMouse))
    {
        newCursor = pointerCursor;
    }

    if (newCursor != currentCursor)
    {
        SDL_SetCursor(newCursor);
        currentCursor = newCursor;
    }
}

static void updateComponentUnderMouse(CupuacuState *state, const int32_t mouseX, const int32_t mouseY)
{
    auto oldComponentUnderMouse = state->componentUnderMouse;
    const auto newComponentUnderMouse = state->rootComponent->findComponentAt(mouseX, mouseY);

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

static void handleResize(CupuacuState *state)
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
        return;
    }

    resizeComponents(state);
}

static void handleMouseMotion(CupuacuState *state, SDL_Event *event)
{
    scaleMouseMotionEvent(state, event);
    state->rootComponent->handleEvent(*event);

    if (state->capturingComponent == nullptr)
    {
        updateComponentUnderMouse(state, event->motion.x, event->motion.y);
    }
}

static void handleMouseDown(CupuacuState *state, SDL_Event *event)
{
    scaleMouseButtonEvent(state, event);

    if (state->capturingComponent == nullptr)
    {
        state->rootComponent->handleEvent(*event);
        return;
    }

    state->capturingComponent->handleEvent(*event);
}

static void handleMouseUp(CupuacuState *state, SDL_Event *event)
{
    scaleMouseButtonEvent(state, event);
    updateComponentUnderMouse(state, event->button.x, event->button.y);

    if (state->capturingComponent == nullptr)
    {
        state->rootComponent->handleEvent(*event);
        return;
    }

    if (!state->capturingComponent->containsAbsoluteCoordinate(event->button.x, event->button.y))
    {
        state->capturingComponent->mouseLeave();
    }

    state->capturingComponent->handleEvent(*event);
    state->capturingComponent = nullptr;
}

static void handleWindowMouseLeave(CupuacuState *state)
{
    if (state->capturingComponent != nullptr)
    {
        return;
    }

    if (state->selection.isActive())
    {
        return;
    }

    for (auto* waveform : state->waveforms)
    {
        waveform->clearHighlight();
    }

    state->componentUnderMouse = nullptr;
}

inline SDL_AppResult handleAppEvent(CupuacuState *state, SDL_Event *event)
{
    switch (event->type)
    {
        case SDL_EVENT_QUIT:
            cleanupCursors();
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            wasMaximized = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            handleResize(state);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            handleWindowMouseLeave(state);
            break;
        case SDL_EVENT_KEY_DOWN:
            handleKeyDown(event, state);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            handleMouseMotion(state, event);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            handleMouseDown(state, event);
            //state->rootComponent->printTree();
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            handleMouseUp(state, event);
            break;
    }

    updateMouseCursor(state);

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        //state->selection.printInfo();
    }

    return SDL_APP_CONTINUE;
}
