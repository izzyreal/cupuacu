#include "TriangleMarker.h"
#include "../CupuacuState.h"

#include "MainView.h"

TriangleMarker::TriangleMarker(CupuacuState* state, TriangleMarkerType typeIn)
    : Component(state, "TriangleMarker"), type(typeIn)
{
}

SDL_FColor TriangleMarker::getColor() const
{
    return SDL_FColor{188/255.f, 188/255.f, 0.f, 1.f};
}

void TriangleMarker::drawTriangle(SDL_Renderer* r,
                                  const SDL_FPoint (&pts)[3],
                                  const SDL_FColor& color)
{
    SDL_Vertex verts[3];

    for (int i = 0; i < 3; ++i)
    {
        verts[i].position = pts[i];
        verts[i].color = color;
        verts[i].tex_coord = {0.f, 0.f};
    }

    const int indices[3] = {0, 1, 2};
    SDL_RenderGeometry(r, nullptr, verts, 3, indices, 3);
}

void TriangleMarker::onDraw(SDL_Renderer* r)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const SDL_FColor color = getColor();

    switch (type) {
        case TriangleMarkerType::CursorTop:
            drawTriangle(r, {SDL_FPoint{w/2, h}, {0,0}, {w,0}}, color);
            break;
        case TriangleMarkerType::CursorBottom:
            drawTriangle(r, {SDL_FPoint{w/2,0}, {0,h}, {w,h}}, color);
            break;
        case TriangleMarkerType::SelectionStartTop:
            drawTriangle(r, {SDL_FPoint{0,0}, {0,h}, {w,0}}, color);
            break;
        case TriangleMarkerType::SelectionStartBottom:
            drawTriangle(r, {SDL_FPoint{0,h}, {0,0}, {w,h}}, color);
            break;
        case TriangleMarkerType::SelectionEndTop:
            drawTriangle(r, {SDL_FPoint{w,0}, {w,h}, {0,0}}, color);
            break;
        case TriangleMarkerType::SelectionEndBottom:
            drawTriangle(r, {SDL_FPoint{w,h}, {w,0}, {0,h}}, color);
            break;
    }
}

bool TriangleMarker::mouseDown(const MouseEvent &e)
{
    const float mouseParentX = e.mouseXf + getXPos();

    if (type == TriangleMarkerType::SelectionStartTop || type == TriangleMarkerType::SelectionStartBottom)
    {
        dragStartSample = state->selection.getStart();
    }
    else if (type == TriangleMarkerType::SelectionEndTop || type == TriangleMarkerType::SelectionEndBottom)
    {
        dragStartSample = state->selection.getEndInt();
    }
    else
    {
        dragStartSample = static_cast<double>(state->cursor);
    }

    const double mouseSample = mouseParentX * state->samplesPerPixel;
    dragMouseOffsetParentX = static_cast<float>(mouseSample - dragStartSample);

    if (state->selection.isActive())
    {
        state->selection.fixOrder();
    }

    return true;
}

bool TriangleMarker::mouseUp(const MouseEvent &e)
{
    if (state->selection.isActive())
    {
        state->cursor = state->selection.getStartInt();
    }

    return true;
}

bool TriangleMarker::mouseMove(const MouseEvent &e)
{
    if (state->capturingComponent != this || !e.buttonState.left)
    {
        return false;
    }

    const float mouseParentX = e.mouseXf + getXPos();
    const double mouseSample = mouseParentX * state->samplesPerPixel;
    const double newSamplePos = mouseSample - dragMouseOffsetParentX;

    switch (type) {
        case TriangleMarkerType::CursorTop:
        case TriangleMarkerType::CursorBottom:
            updateCursorPos(state, newSamplePos);
            break;
        case TriangleMarkerType::SelectionStartTop:
        case TriangleMarkerType::SelectionStartBottom:
            state->selection.setValue1(newSamplePos);
            break;
        case TriangleMarkerType::SelectionEndTop:
        case TriangleMarkerType::SelectionEndBottom:
            state->selection.setValue2(newSamplePos);
            break;
    }

    dynamic_cast<MainView*>(getParent())->updateTriangleMarkerBounds();
    getParent()->setDirtyRecursive();

    return true;
}

