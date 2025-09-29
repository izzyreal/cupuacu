#include "TriangleMarker.h"
#include "../CupuacuState.h"

#include "MainView.h"

#include <cmath>

TriangleMarker::TriangleMarker(CupuacuState* state, TriangleMarkerType typeIn)
    : Component(state, "TriangleMarker"), type(typeIn) {}

SDL_FColor TriangleMarker::getColor() const {
    return SDL_FColor{188/255.f, 188/255.f, 0.f, 1.f};
}

void TriangleMarker::drawTriangle(SDL_Renderer* r,
                                  const SDL_FPoint (&pts)[3],
                                  const SDL_FColor& color) {
    SDL_Vertex verts[3];
    for (int i = 0; i < 3; ++i) {
        verts[i].position = pts[i];
        verts[i].color = color;
        verts[i].tex_coord = {0.f, 0.f};
    }
    const int indices[3] = {0, 1, 2};
    SDL_RenderGeometry(r, nullptr, verts, 3, indices, 3);
}

void TriangleMarker::onDraw(SDL_Renderer* r) {

    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const SDL_FColor color = getColor();

    switch (type) {
        case TriangleMarkerType::CursorTop: {
            SDL_FPoint pts[3] = {
                {w/2, h},   // was 0
                {0, 0},     // was h
                {w, 0}      // was h
            };
            drawTriangle(r, pts, color);
            break;
        }
        case TriangleMarkerType::CursorBottom: {
            SDL_FPoint pts[3] = {
                {w/2, 0},   // tip up
                {0, h},     // bottom left
                {w, h}      // bottom right
            };
            drawTriangle(r, pts, color);
            break;
        }                                            
        case TriangleMarkerType::SelectionStartTop: {
            SDL_FPoint pts[3] = {
                {0, 0},     // was h
                {0, h},     // was 0
                {w, 0}      // was h
            };
            drawTriangle(r, pts, color);
            break;
        }
        case TriangleMarkerType::SelectionStartBottom: {
            SDL_FPoint pts[3] = {
                {0, h},     // was 0
                {0, 0},     // was h
                {w, h}      // was 0
            };
            drawTriangle(r, pts, color);
            break;
        }
        case TriangleMarkerType::SelectionEndTop: {
            SDL_FPoint pts[3] = {
                {w, 0},     // was h
                {w, h},     // was 0
                {0, 0}      // was h
            };
            drawTriangle(r, pts, color);
            break;
        }
        case TriangleMarkerType::SelectionEndBottom: {
            SDL_FPoint pts[3] = {
                {w, h},     // was 0
                {w, 0},     // was h
                {0, h}      // was 0
            };
            drawTriangle(r, pts, color);
            break;
        }
    }
}

bool TriangleMarker::mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY)
{
    dragOffsetX = mouseX;
    return true;
}

bool TriangleMarker::mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY)
{
    return true;
}

bool TriangleMarker::mouseMove(const int32_t mouseX, const int32_t mouseY,
                               const float mouseRelY, const bool leftButtonIsDown) {
    if (state->capturingComponent != this || !leftButtonIsDown) {
        return false;
    }

    updateStateFromDrag(mouseX + getXPos() - dragOffsetX);
    return true;
}

void TriangleMarker::updateStateFromDrag(int32_t newX) {
    const double samplesPerPx = state->samplesPerPixel;
    if (samplesPerPx <= 0.0) return;

    switch (type) {
        case TriangleMarkerType::CursorTop:
        case TriangleMarkerType::CursorBottom:
            break;
        case TriangleMarkerType::SelectionStartTop:
        case TriangleMarkerType::SelectionStartBottom:
            break;
        case TriangleMarkerType::SelectionEndTop:
        case TriangleMarkerType::SelectionEndBottom:
            break;
    }

    const double sampleOffset = state->sampleOffset;

    const int sample = static_cast<int>(std::round(sampleOffset + newX * samplesPerPx));

    switch (type) {
        case TriangleMarkerType::CursorTop:
        case TriangleMarkerType::CursorBottom:
            updateCursorPos(state, sample);
            break;
        case TriangleMarkerType::SelectionStartTop:
        case TriangleMarkerType::SelectionStartBottom:
            state->selection.setValue1(sample);
            break;
        case TriangleMarkerType::SelectionEndTop:
        case TriangleMarkerType::SelectionEndBottom:
            state->selection.setValue2(sample);
            break;
    }

    dynamic_cast<MainView*>(getParent())->updateTriangleMarkerBounds();
    getParent()->setDirtyRecursive();
}
