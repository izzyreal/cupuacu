#pragma once

#include <SDL3/SDL.h>
#include <string>

#include "Component.hpp"
#include "State.hpp"
#include "UiScale.hpp"

namespace cupuacu::gui
{
    enum class TextOverflowMode
    {
        Clip,
        Ellipsis,
    };

    class Label : public Component
    {
    private:
        std::string text;
        bool centerHorizontally = false;
        bool centerVertically = true;
        float margin = 0;
        int pointSize = 8;
        TextOverflowMode overflowMode = TextOverflowMode::Clip;
        bool textTruncated = false;

        // --- cache ---
        SDL_Texture *cachedTexture = nullptr;
        int cachedW = 0;
        int cachedH = 0;
        std::string cachedText;
        std::string cachedRenderedText;
        int cachedPointSize = 0;
        int cachedOpacity = 0;
        int cachedAvailableWidth = 0;
        TextOverflowMode cachedOverflowMode = TextOverflowMode::Clip;

        void updateTexture(SDL_Renderer *renderer, int availableWidth);

        uint8_t opacity = 255;

    public:
        Label(State *state, const std::string &textToUse = "");

        ~Label();

        void setOpacity(const uint8_t opacity);

        std::string getText()
        {
            return text;
        }
        void setText(const std::string &newText)
        {
            if (text == newText)
            {
                return;
            }
            text = newText;
            setDirty();
        }
        void setMargin(const int m)
        {
            margin = m;
            setDirty();
        }
        void setFontSize(const int p)
        {
            pointSize = p;
            setDirty();
        }
        uint8_t getEffectiveFontSize() const
        {
            return scaleFontPointSize(state, pointSize);
        }
        void setCenterHorizontally(const bool centerHorizontallyToUse)
        {
            centerHorizontally = centerHorizontallyToUse;
            setDirty();
        }
        void setOverflowMode(const TextOverflowMode overflowModeToUse)
        {
            if (overflowMode == overflowModeToUse)
            {
                return;
            }
            overflowMode = overflowModeToUse;
            setDirty();
        }
        bool isTextTruncated() const
        {
            return textTruncated;
        }

        void onDraw(SDL_Renderer *renderer) override;
    };
} // namespace cupuacu::gui
