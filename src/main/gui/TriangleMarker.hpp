#pragma once
#include "Component.hpp"

namespace cupuacu::gui
{
    enum class TriangleMarkerType
    {
        CursorTop,
        CursorBottom,
        SelectionStartTop,
        SelectionStartBottom,
        SelectionEndTop,
        SelectionEndBottom
    };

    class TriangleMarker : public Component
    {
    public:
        TriangleMarker(State *state, TriangleMarkerType type);

        void onDraw(SDL_Renderer *r) override;
        bool mouseMove(const MouseEvent &) override;

        bool mouseDown(const MouseEvent &) override;
        bool mouseUp(const MouseEvent &) override;

        TriangleMarkerType getType() const
        {
            return type;
        }

    private:
        TriangleMarkerType type;

        double dragStartSample = 0.0;
        float dragMouseOffsetParentX = 0.f;

        static void drawTriangle(SDL_Renderer *r, const SDL_FPoint (&pts)[3],
                                 const SDL_FColor &color);
        static SDL_FColor getColor();
    };
} // namespace cupuacu::gui
