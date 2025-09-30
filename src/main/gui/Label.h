#pragma once

#include <SDL3/SDL.h>
#include <string>

#include "Component.h"

class Label : public Component {
private:
    std::string text;
    bool centerHorizontally = false;
    bool centerVertically = true;
    int margin = 15;

public:
    Label(CupuacuState* state,
          const std::string& textToUse);

    void setText(const std::string& newText) { text = newText; setDirty(); }
    void setMargin(int m) { margin = m; setDirty(); }
    void setCenterHorizontally(const bool centerHorizontallyToUse) { centerHorizontally = centerHorizontallyToUse; setDirty(); }

    void onDraw(SDL_Renderer* renderer) override;
};
