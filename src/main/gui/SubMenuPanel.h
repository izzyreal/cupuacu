#include "RoundedRect.h"

#include "Component.h"

class SubMenuPanel : public Component
{
public:
    SubMenuPanel(CupuacuState* state, const std::string& id)
        : Component(state, id)
    {
         disableParentClipping();
        }

    void onDraw(SDL_Renderer* renderer) override
    {
        SDL_Color panelBg     = { 50, 50, 50, 255 };
        SDL_Color panelBorder = { 90, 90, 90, 255 };
        SDL_FRect rect = getLocalBounds();

        drawRoundedRect(renderer, rect, 12.0f, panelBg);
        drawRoundedRectOutline(renderer, rect, 12.0f, panelBorder);
    }
};

