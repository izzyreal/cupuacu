#pragma once

#include <SDL3/SDL.h>
#include <string>

#include "Component.h"

namespace cupuacu::gui
{

    class Label : public Component
    {
    private:
        std::string text;
        bool centerHorizontally = false;
        bool centerVertically = true;
        float margin = 0;
        int pointSize = 8;

        // --- cache ---
        SDL_Texture *cachedTexture = nullptr;
        int cachedW = 0;
        int cachedH = 0;
        std::string cachedText;
        int cachedPointSize = 0;
        int cachedOpacity = 0;

        void updateTexture(SDL_Renderer *renderer);

        uint8_t opacity = 255;

    public:
        Label(cupuacu::State *state, const std::string &textToUse = "");

        ~Label();

        void setOpacity(const uint8_t opacity);

        std::string getText()
        {
            return text;
        }
        void setText(const std::string &newText)
        {
            text = newText;
            setDirty();
        }
        void setMargin(int m)
        {
            margin = m;
            setDirty();
        }
        void setFontSize(int p)
        {
            pointSize = p;
            setDirty();
        }
        uint8_t getEffectiveFontSize()
        {
            return (float)pointSize / state->pixelScale;
        }
        void setCenterHorizontally(const bool centerHorizontallyToUse)
        {
            centerHorizontally = centerHorizontallyToUse;
            setDirty();
        }

        void onDraw(SDL_Renderer *renderer) override;
    };
} // namespace cupuacu::gui
