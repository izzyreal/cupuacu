#pragma once

#include "../State.hpp"
#include "../actions/markers/EditCommands.hpp"

#include "Component.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace cupuacu::gui
{
    enum class DocumentMarkerHandleEdge
    {
        Top,
        Bottom,
    };

    class DocumentMarkerHandle : public Component
    {
    public:
        explicit DocumentMarkerHandle(State *stateToUse, uint64_t markerIdToUse,
                                      DocumentMarkerHandleEdge edgeToUse);

        uint64_t getMarkerId() const
        {
            return markerId;
        }

        DocumentMarkerHandleEdge getEdge() const
        {
            return edge;
        }

        bool mouseDown(const MouseEvent &) override;
        bool mouseMove(const MouseEvent &) override;
        bool mouseUp(const MouseEvent &) override;
        std::string getTooltipText() const override;
        void onDraw(SDL_Renderer *renderer) override;

    private:
        uint64_t markerId = 0;
        DocumentMarkerHandleEdge edge = DocumentMarkerHandleEdge::Top;
        float dragMouseOffsetParentX = 0.0f;
        bool isDragging = false;
        bool openEditorOnMouseUp = false;
        std::shared_ptr<actions::markers::SetMarkerState> undoable;

        SDL_FColor getColor() const;
        void drawTriangle(SDL_Renderer *renderer, const SDL_FPoint (&pts)[3],
                          const SDL_FColor &color) const;
    };
} // namespace cupuacu::gui
