#include "MenuBar.h"

#include "../CupuacuState.h"
#include "Menu.h"

#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Save.h"

MenuBar::MenuBar(CupuacuState *stateToUse) : Component(stateToUse, "MenuBar")
{
    disableParentClipping();
    fileMenu = emplaceChildAndSetDirty<Menu>(state, "File");
    viewMenu = emplaceChildAndSetDirty<Menu>(state, "View");
    fileMenu->enableDepthIs0();
    viewMenu->enableDepthIs0();

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
}

void MenuBar::hideSubMenus()
{
    fileMenu->hideSubMenus();
    viewMenu->hideSubMenus();
}

void MenuBar::resized()
{
    float scale = 4.0f / state->pixelScale;

    int fileW = int(40 * scale);
    int viewW = int(100 * scale);
    int h = getHeight();

    fileMenu->setBounds(0, 0, fileW, h);
    viewMenu->setBounds(fileW, 0, viewW, h);
}

void MenuBar::mouseEnter()
{
    setDirty();
}

