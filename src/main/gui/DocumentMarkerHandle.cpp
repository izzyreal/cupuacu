#include "DocumentMarkerHandle.hpp"

#include "../actions/markers/Dialogs.hpp"
#include "../actions/markers/EditCommands.hpp"

#include "MainViewAccess.hpp"
#include "Waveform.hpp"
#include "Window.hpp"

#include <algorithm>
#include <cmath>

using namespace cupuacu::gui;

DocumentMarkerHandle::DocumentMarkerHandle(State *stateToUse,
                                           const uint64_t markerIdToUse,
                                           const DocumentMarkerHandleEdge edgeToUse)
    : Component(stateToUse,
                "DocumentMarkerHandle:" +
                    std::string(edgeToUse == DocumentMarkerHandleEdge::Top
                                    ? "Top:"
                                    : "Bottom:") +
                    std::to_string(markerIdToUse)),
      markerId(markerIdToUse), edge(edgeToUse)
{
}

SDL_FColor DocumentMarkerHandle::getColor() const
{
    const bool isSelected =
        state && state->getActiveViewState().selectedMarkerId == markerId;
    if (isSelected)
    {
        return SDL_FColor{1.0f, 0.45f, 0.0f, 1.0f};
    }

    return SDL_FColor{0.9f, 0.3f, 0.1f, 1.0f};
}

void DocumentMarkerHandle::drawTriangle(SDL_Renderer *renderer,
                                        const SDL_FPoint (&pts)[3],
                                        const SDL_FColor &color) const
{
    SDL_Vertex vertices[3];
    for (int index = 0; index < 3; ++index)
    {
        vertices[index].position = pts[index];
        vertices[index].color = color;
        vertices[index].tex_coord = {0.0f, 0.0f};
    }

    constexpr int indices[3] = {0, 1, 2};
    SDL_RenderGeometry(renderer, nullptr, vertices, 3, indices, 3);
}

void DocumentMarkerHandle::onDraw(SDL_Renderer *renderer)
{
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    const SDL_FColor color = getColor();

    if (edge == DocumentMarkerHandleEdge::Top)
    {
        drawTriangle(renderer,
                     {SDL_FPoint{width * 0.5f, height}, SDL_FPoint{0.0f, 0.0f},
                      SDL_FPoint{width, 0.0f}},
                     color);
        return;
    }

    drawTriangle(renderer,
                 {SDL_FPoint{width * 0.5f, 0.0f}, SDL_FPoint{0.0f, height},
                  SDL_FPoint{width, height}},
                 color);
}

bool DocumentMarkerHandle::mouseDown(const MouseEvent &event)
{
    if (!event.buttonState.left || !state)
    {
        return false;
    }

    auto marker = actions::markers::currentMarkerSnapshot(state, markerId);
    if (!marker.has_value())
    {
        return false;
    }

    state->getActiveViewState().selectedMarkerId = markerId;
    actions::markers::refreshMarkerUi(state);

    if (event.numClicks >= 2)
    {
        openEditorOnMouseUp = true;
        return true;
    }

    const auto &viewState = state->getActiveViewState();
    const float mouseParentX = event.mouseXf + getXPos();
    const double markerParentX =
        Waveform::getDoubleXPosForSampleIndex(marker->marker.frame,
                                              viewState.sampleOffset,
                                              viewState.samplesPerPixel);
    dragMouseOffsetParentX =
        mouseParentX - static_cast<float>(markerParentX);
    isDragging = true;
    undoable = std::make_shared<actions::markers::SetMarkerState>(
        state, *marker, *marker, "Move marker");
    return true;
}

bool DocumentMarkerHandle::mouseMove(const MouseEvent &event)
{
    if (!isDragging || !event.buttonState.left || !state || !getWindow() ||
        getWindow()->getCapturingComponent() != this)
    {
        return false;
    }

    auto marker = actions::markers::currentMarkerSnapshot(state, markerId);
    if (!marker.has_value())
    {
        return false;
    }

    const auto &viewState = state->getActiveViewState();
    const float mouseParentX = event.mouseXf + getXPos();
    const double samplePosition = Waveform::getDoubleSampleIndexForXPos(
        mouseParentX - dragMouseOffsetParentX, viewState.sampleOffset,
        viewState.samplesPerPixel);
    const int64_t frame = std::clamp(
        static_cast<int64_t>(std::llround(samplePosition)), int64_t{0},
        state->getActiveDocumentSession().document.getFrameCount());

    state->getActiveDocumentSession().document.setMarkerFrame(markerId, frame);
    marker->marker.frame = frame;
    marker->selectedMarkerId = markerId;
    undoable->setNewState(*marker);
    actions::markers::refreshMarkerUi(state);
    return true;
}

bool DocumentMarkerHandle::mouseUp(const MouseEvent &)
{
    if (openEditorOnMouseUp)
    {
        openEditorOnMouseUp = false;
        actions::markers::showMarkerEditorDialog(state, markerId);
        return true;
    }

    if (!isDragging)
    {
        return false;
    }

    isDragging = false;
    if (undoable && undoable->getOldState().marker.frame !=
                        undoable->getNewState().marker.frame)
    {
        state->addUndoable(undoable);
        undoable->updateGui();
    }
    undoable.reset();
    return true;
}

std::string DocumentMarkerHandle::getTooltipText() const
{
    if (!state)
    {
        return {};
    }

    const auto marker =
        actions::markers::findMarkerById(state->getActiveDocumentSession().document,
                                         markerId);
    if (!marker.has_value())
    {
        return {};
    }

    std::string text = "Marker at " + std::to_string(marker->frame);
    if (!marker->label.empty())
    {
        text += "\n" + marker->label;
    }
    return text;
}
