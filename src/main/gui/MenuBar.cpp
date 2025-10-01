#include "MenuBar.h"

#include "../CupuacuState.h"
#include "OpaqueRect.h"
#include "Menu.h"
#include "../ResourceUtil.hpp"

#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Save.h"

MenuBar::MenuBar(CupuacuState *stateToUse) : Component(stateToUse, "MenuBar")
{
    background = emplaceChild<OpaqueRect>(state, Colors::background);
    disableParentClipping();
    fileMenu = emplaceChild<Menu>(state, "File");
    viewMenu = emplaceChild<Menu>(state, "View");

#ifdef __APPLE__
    const std::string openText{"Open (Cmd + O)"};
#else
    const std::string openText{"Open (Ctrl + O)"};
#endif
    fileMenu->addSubMenu(state, openText, [&]{
                showOpenFileDialog(state);
            });
#ifdef __APPLE__
    const std::string overwriteText{"Overwrite (Cmd + S)"};
#else
    const std::string overwriteText{"Overwrite (Ctrl + S)"};
#endif
    fileMenu->addSubMenu(state, overwriteText, [&]{
                overwrite(state);
            });

    viewMenu->addSubMenu(state, "Reset zoom (Esc)", [&]{
                resetZoom(state);
            });
    viewMenu->addSubMenu(state, "Zoom out horiz. (Q)", [&]{
                tryZoomOutHorizontally(state);
            });
    viewMenu->addSubMenu(state, "Zoom in horiz. (W)", [&]{
                tryZoomInHorizontally(state);
            });
    viewMenu->addSubMenu(state, "Zoom out vert. (E)", [&]{
                tryZoomOutVertically(state, 1);
            });
    viewMenu->addSubMenu(state, "Zoom in vert. (R)", [&]{
                zoomInVertically(state, 1);
            });

    logoData = get_resource_data("cupuacu-logo1.bmp");
}

void MenuBar::onDraw(SDL_Renderer* renderer)
{
    if (!logoTexture && !logoData.empty())
    {
        SDL_IOStream* io = SDL_IOFromConstMem(logoData.data(),
                                              static_cast<int>(logoData.size()));
        if (io)
        {
            SDL_Surface* surf = SDL_LoadBMP_IO(io, true);
            if (surf)
            {
                logoTexture = SDL_CreateTextureFromSurface(renderer, surf);
                logoW = surf->w;
                logoH = surf->h;
                SDL_SetTextureScaleMode(logoTexture, SDL_SCALEMODE_NEAREST);
                SDL_DestroySurface(surf);
                resized();
            }
        }
    }

    SDL_FRect dst;
    dst.x = 0.0f;
    dst.y = 0.0f;
    dst.w = float(logoW);
    dst.h = float(getHeight());

    float scale = float(getHeight()) / float(logoH);
    dst.w = logoW * scale;

    Helpers::fillRect(renderer, dst, Colors::background);
    SDL_RenderTexture(renderer, logoTexture, nullptr, &dst);
}

void MenuBar::hideSubMenus()
{
    fileMenu->hideSubMenus();
    viewMenu->hideSubMenus();
    state->rootComponent->setDirty();
    openSubMenuOnMouseOver = false;
}

void MenuBar::resized()
{
    float scale = 4.0f / state->pixelScale;

    int fileW = int(40 * scale);
    int viewW = int(100 * scale);
    int h = getHeight();

    int logoSpace = 0;
    if (!logoData.empty())
    {
        float aspect = logoH ? (float)logoW / (float)logoH : 1.0f;
        logoSpace = int(h * aspect);
    }

    fileMenu->setBounds(logoSpace, 0, fileW, h);
    viewMenu->setBounds(logoSpace + fileW, 0, viewW, h);

    SDL_Rect backgroundBounds = getLocalBounds();
    backgroundBounds.x = viewMenu->getBounds().x + viewW;
    backgroundBounds.w = getWidth() - backgroundBounds.x;

    background->setBounds(backgroundBounds);
}
void MenuBar::mouseEnter()
{
    setDirty();
}

Menu* MenuBar::getOpenMenu()
{
    if (fileMenu->isOpen()) return fileMenu;
    if (viewMenu->isOpen()) return viewMenu;
    return nullptr;
}

