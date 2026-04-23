#include "gui/TriangleMarker.hpp"

#include "State.hpp"

#include "gui/SnapPlanning.hpp"
#include "gui/TriangleMarkerInteractionPlanning.hpp"
#include "gui/Window.hpp"

using namespace cupuacu::gui;

TriangleMarker::TriangleMarker(State *state, TriangleMarkerType typeIn)
    : Component(state,
                [typeIn]()
                {
                    switch (typeIn)
                    {
                        case TriangleMarkerType::CursorTop:
                            return "TriangleMarker:CursorTop";
                        case TriangleMarkerType::CursorBottom:
                            return "TriangleMarker:CursorBottom";
                        case TriangleMarkerType::SelectionStartTop:
                            return "TriangleMarker:SelectionStartTop";
                        case TriangleMarkerType::SelectionStartBottom:
                            return "TriangleMarker:SelectionStartBottom";
                        case TriangleMarkerType::SelectionEndTop:
                            return "TriangleMarker:SelectionEndTop";
                        case TriangleMarkerType::SelectionEndBottom:
                            return "TriangleMarker:SelectionEndBottom";
                    }
                    return "TriangleMarker";
                }()),
      type(typeIn)
{
}

SDL_FColor TriangleMarker::getColor()
{
    return SDL_FColor{188 / 255.f, 188 / 255.f, 0.f, 1.f};
}

void TriangleMarker::drawTriangle(SDL_Renderer *r, const SDL_FPoint (&pts)[3],
                                  const SDL_FColor &color)
{
    SDL_Vertex verts[3];

    for (int i = 0; i < 3; ++i)
    {
        verts[i].position = pts[i];
        verts[i].color = color;
        verts[i].tex_coord = {0.f, 0.f};
    }

    constexpr int indices[3] = {0, 1, 2};
    SDL_RenderGeometry(r, nullptr, verts, 3, indices, 3);
}

void TriangleMarker::onDraw(SDL_Renderer *r)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const SDL_FColor color = getColor();

    switch (type)
    {
        case TriangleMarkerType::CursorTop:
            drawTriangle(r, {SDL_FPoint{w / 2, h}, {0, 0}, {w, 0}}, color);
            break;
        case TriangleMarkerType::CursorBottom:
            drawTriangle(r, {SDL_FPoint{w / 2, 0}, {0, h}, {w, h}}, color);
            break;
        case TriangleMarkerType::SelectionStartTop:
            drawTriangle(r, {SDL_FPoint{0, 0}, {0, h}, {w, 0}}, color);
            break;
        case TriangleMarkerType::SelectionStartBottom:
            drawTriangle(r, {SDL_FPoint{0, h}, {0, 0}, {w, h}}, color);
            break;
        case TriangleMarkerType::SelectionEndTop:
            drawTriangle(r, {SDL_FPoint{w, 0}, {w, h}, {0, 0}}, color);
            break;
        case TriangleMarkerType::SelectionEndBottom:
            drawTriangle(r, {SDL_FPoint{w, h}, {w, 0}, {0, h}}, color);
            break;
    }
}

bool TriangleMarker::mouseDown(const MouseEvent &e)
{
    auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();
    const float mouseParentX = e.mouseXf + getXPos();
    const auto plan = planTriangleMarkerMouseDown(
        type, session.selection.getStart(),
        session.selection.getEndExclusiveInt(), session.cursor, mouseParentX,
        viewState.samplesPerPixel, session.selection.isActive());
    dragStartSample = plan.dragStartSample;
    dragMouseOffsetParentX = plan.dragMouseOffsetParentX;
    if (plan.shouldFixSelectionOrder)
    {
        session.selection.fixOrder();
    }

    return true;
}

bool TriangleMarker::mouseUp(const MouseEvent &e)
{
    return true;
}

bool TriangleMarker::mouseMove(const MouseEvent &e)
{
    auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();
    if (!getWindow() || getWindow()->getCapturingComponent() != this ||
        !e.buttonState.left)
    {
        return false;
    }

    const float mouseParentX = e.mouseXf + getXPos();
    const double newSamplePos = planTriangleMarkerDraggedSamplePosition(
        mouseParentX, viewState.samplesPerPixel, dragMouseOffsetParentX);

    const bool selectionWasActive = session.selection.isActive();

    switch (type)
    {
        case TriangleMarkerType::CursorTop:
        case TriangleMarkerType::CursorBottom:
        {
            const int64_t rawFrame = std::clamp(
                static_cast<int64_t>(std::llround(newSamplePos)), int64_t{0},
                session.document.getFrameCount());
            const int64_t snappedFrame = snapSamplePosition(
                state, rawFrame, std::nullopt, false, std::nullopt,
                viewState.sampleOffset,
                viewState.samplesPerPixel, Waveform::getWaveformWidth(state));
            updateCursorPos(state, snappedFrame);
            break;
        }
        case TriangleMarkerType::SelectionStartTop:
        case TriangleMarkerType::SelectionStartBottom:
        {
            const int64_t rawFrame = std::clamp(
                static_cast<int64_t>(std::llround(newSamplePos)), int64_t{0},
                session.document.getFrameCount());
            const int64_t snappedFrame = snapSamplePosition(
                state, rawFrame, std::nullopt, true, SnapSelectionEdge::Start,
                viewState.sampleOffset, viewState.samplesPerPixel,
                Waveform::getWaveformWidth(state));
            session.selection.setValue1(planTriangleMarkerSelectionValue(
                snappedFrame, session.selection.getEndExclusiveInt(),
                selectionWasActive));
            break;
        }
        case TriangleMarkerType::SelectionEndTop:
        case TriangleMarkerType::SelectionEndBottom:
        {
            const int64_t rawFrame = std::clamp(
                static_cast<int64_t>(std::llround(newSamplePos)), int64_t{0},
                session.document.getFrameCount());
            const int64_t snappedFrame = snapSamplePosition(
                state, rawFrame, std::nullopt, true, SnapSelectionEdge::End,
                viewState.sampleOffset, viewState.samplesPerPixel,
                Waveform::getWaveformWidth(state));
            session.selection.setValue2(planTriangleMarkerSelectionValue(
                snappedFrame, session.selection.getStartInt(),
                selectionWasActive));
            break;
        }
    }

    return true;
}
