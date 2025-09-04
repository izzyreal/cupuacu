#pragma once

#include "Component.h"
#include "text.h"
#include "../CupuacuState.h"
#include "../file_loading.h"
#include "WaveformComponent.h"

#include <SDL3/SDL.h>

class Menu : public Component {
    private:
        bool currentlyOpen = false;
        const std::string menuName;
        std::vector<Menu*> subMenus;
        const std::function<void()> action;

    public:
        Menu(CupuacuState *state, const std::string menuNameToUse, const std::function<void()> actionToUse = {}) :
            Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse), action(actionToUse)
        {
        }

        void showSubMenus()
        {
            if (subMenus.empty())
            {
                return;
            }

            int subMenuYPos = getHeight();

            for (auto &subMenu : subMenus)
            {
                subMenu->setBounds(5, subMenuYPos, 100, 20);
                subMenuYPos += 20;
            }

            currentlyOpen = true;
        }

        void hideSubMenus()
        {
            for (auto &subMenu : subMenus)
            {
                subMenu->setBounds(0, 0, 0, 0);
            }

            currentlyOpen = false;
        }

        template <typename... Args>
        void addSubMenu(Args&&... args)
        {
            auto subMenu = emplaceChildAndSetDirty<Menu>(std::forward<Args>(args)...);
            subMenu->setBounds(0, 0, 0, 0);
            subMenus.push_back(subMenu);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const uint8_t bg = isMouseOver() ? 80 : 40;
            SDL_SetRenderDrawColor(renderer, bg, bg, bg, 255);
            SDL_FRect r{0, 0, (float)getWidth(), (float)getHeight() };
            SDL_RenderFillRect(renderer, &r);
            const uint8_t fontPointSize = state->menuFontSize / state->hardwarePixelsPerAppPixel;
            renderText(renderer, menuName, fontPointSize);
        }

        bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override
        {
            const bool wasCurrentlyOpen = currentlyOpen;

            if (subMenus.empty())
            {
                if (action)
                {
                    action();
                }
                state->hideSubMenus();
                return true;
            }

            if (wasCurrentlyOpen)
            {
                state->hideSubMenus();
                return true;
            }

            state->hideSubMenus();
            showSubMenus();

            return true;
        }

        bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override
        {
            return true;
        }

        void mouseLeave() override
        {
            setDirty();
        }

        void mouseEnter() override
        {
            setDirty();
        }
};

const SDL_DialogFileFilter filters[] = {
    { "WAV audio", "wav" }
};

static auto initialPath = SDL_GetUserFolder(SDL_FOLDER_HOME);

class MenuBar : public Component {

    private:
        Menu* fileMenu = nullptr;
        Menu* viewMenu = nullptr;

        static void fileDialogCallback(void *userdata, const char * const *filelist, int filter)
        {
            if (!filelist) {
                SDL_Log("An error occured: %s", SDL_GetError());
                return;
            } else if (!*filelist) {
                SDL_Log("The user did not select any file.");
                SDL_Log("Most likely, the dialog was canceled.");
                return;
            }

            std::string absoluteFilePath;

            while (*filelist) {
                //SDL_Log("Full path to selected file: '%s'", *filelist);
                absoluteFilePath = *filelist;
                break;
                filelist++;
            }

            auto state = (CupuacuState*)userdata;

            state->currentFile = absoluteFilePath;

            loadSampleData(state);
            const auto waveformWidth = state->waveformComponent->getWidth();
            state->samplesPerPixel = state->sampleDataL.size() / (float) waveformWidth;
            resetWaveformState(state);
            dynamic_cast<WaveformComponent*>(state->waveformComponent)->updateSamplePoints();
            state->waveformComponent->setDirty();
        }

    public:
        MenuBar(CupuacuState *stateToUse) : Component(stateToUse, "MenuBar")
        {
            fileMenu = emplaceChildAndSetDirty<Menu>(state, "File");
            viewMenu = emplaceChildAndSetDirty<Menu>(state, "View");

            fileMenu->addSubMenu(state, "Load", [&]{
                        SDL_ShowOpenFileDialog(fileDialogCallback, state, NULL, filters, 1, NULL, false);
                    });
            fileMenu->addSubMenu(state, "Save", [&]{
                        printf("Saving a file\n");
                    });

            viewMenu->addSubMenu(state, "Zoom in (W)", [&]{
                        printf("Zooming in...\n");
                    });
            viewMenu->addSubMenu(state, "Zoom out (Q)", [&]{
                        printf("Zooming out...\n");
                    });
        }

        void hideSubMenus()
        {
            fileMenu->hideSubMenus();
            viewMenu->hideSubMenus();
        }

        void resized() override
        {
            fileMenu->setBounds(0, 0, 40, getHeight());
            viewMenu->setBounds(fileMenu->getWidth(), 0, 100, getHeight());
        }

        void mouseEnter() override
        {
            setDirty();
        }
};

