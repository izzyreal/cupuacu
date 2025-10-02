#pragma once

#include <SDL3/SDL.h>
#include <string>

#include "Component.h"

class Label : public Component {
private:
    std::string text;
    bool centerHorizontally = false;
    bool centerVertically   = true;
    float margin            = 15;
    int pointSize           = 8;

    // --- cache ---
    SDL_Texture* cachedTexture = nullptr;
    int cachedW = 0;
    int cachedH = 0;
    std::string cachedText;
    int cachedPointSize = 0;

    void updateTexture(SDL_Renderer* renderer);

public:
    Label(CupuacuState* state,
          const std::string& textToUse);

    ~Label();

    void setText(const std::string& newText) { text = newText; setDirty(); }
    void setMargin(int m) { margin = m; setDirty(); }
    void setFontSize(int p) { pointSize = p; setDirty(); }
    void setCenterHorizontally(const bool centerHorizontallyToUse) { centerHorizontally = centerHorizontallyToUse; setDirty(); }

    void onDraw(SDL_Renderer* renderer) override;
};

