#pragma once
#include "Component.h"

enum class TriangleMarkerType {
    CursorTop,
    CursorBottom,
    SelectionStartTop,
    SelectionStartBottom,
    SelectionEndTop,
    SelectionEndBottom
};

class TriangleMarker : public Component {
public:
    TriangleMarker(CupuacuState* state, TriangleMarkerType type);

    void onDraw(SDL_Renderer* r) override;
    bool mouseMove(const MouseEvent&) override;

    bool mouseDown(const MouseEvent&) override;
    bool mouseUp(const MouseEvent&) override;

    TriangleMarkerType getType() const { return type; }

private:
    int dragOffsetX = 0;
    TriangleMarkerType type;

    void drawTriangle(SDL_Renderer* r, const SDL_FPoint (&pts)[3], const SDL_FColor& color);
    SDL_FColor getColor() const;

    void updateStateFromDrag(int32_t newX);
};

