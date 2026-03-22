#include <SDL3/SDL.h>
#include "../State.hpp"
#include "audio/AudioDevices.hpp"
#include "Waveform.hpp"
#include "WaveformRefresh.hpp"
#include "Window.hpp"
#include "../actions/ShowOpenFileDialog.hpp"
#include "../actions/DocumentLifecycle.hpp"
#include "../actions/DocumentTabs.hpp"
#include "../actions/Play.hpp"
#include "../actions/Zoom.hpp"
#include "../actions/Save.hpp"
#include "../actions/audio/Copy.hpp"
#include "../actions/audio/Cut.hpp"
#include "../actions/audio/Paste.hpp"
#include "../actions/audio/Trim.hpp"
#include "../actions/audio/EditCommands.hpp"

namespace cupuacu::gui
{

    static void updateWaveforms(State *state)
    {
        refreshWaveforms(state, true, true);
    }

    static void handleKeyDown(SDL_Event *event, State *state)
    {
        auto &viewState = state->getActiveViewState();
        auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
        uint8_t multiplier = 1;
        const uint8_t multiplierFactor = 12 / state->pixelScale;

        if (event->key.scancode == SDL_SCANCODE_ESCAPE)
        {
            actions::resetZoom(state);
            updateWaveforms(state);
            return;
        }

        if (event->key.mod & SDL_KMOD_SHIFT)
        {
            multiplier *= multiplierFactor;
        }
        if (event->key.mod & SDL_KMOD_ALT)
        {
            multiplier *= multiplierFactor;
        }
        if (event->key.mod & SDL_KMOD_CTRL)
        {
            multiplier *= multiplierFactor;
        }

        const auto waveformWidth = Waveform::getWaveformWidth(state);
        const bool primaryModifierHeld =
#if __APPLE__
            (event->key.mod & SDL_KMOD_GUI) != 0;
#else
            (event->key.mod & SDL_KMOD_CTRL) != 0;
#endif

        if (event->key.scancode == SDL_SCANCODE_Q)
        {
            if (actions::tryZoomOutHorizontally(state))
            {
                updateWaveforms(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_W)
        {
            if (primaryModifierHeld)
            {
                actions::closeActiveTab(state);
            }
            else if (actions::tryZoomInHorizontally(state))
            {
                updateWaveforms(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_E)
        {
            if (actions::tryZoomOutVertically(state, multiplier))
            {
                updateWaveforms(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_R)
        {
            actions::zoomInVertically(state, multiplier);
            updateWaveforms(state);
        }
        else if (event->key.scancode == SDL_SCANCODE_Z)
        {
            if (primaryModifierHeld)
            {
                if (event->key.mod & SDL_KMOD_SHIFT)
                {
                    state->redo();
                }
                else
                {
                    state->undo();
                }

                return;
            }

            if (actions::tryZoomSelection(state))
            {
                updateWaveforms(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_LEFT)
        {
            if (viewState.sampleOffset == 0)
            {
                return;
            }

            const int64_t oldSampleOffset = viewState.sampleOffset;

            updateSampleOffset(state,
                               viewState.sampleOffset -
                                   std::max(viewState.samplesPerPixel, 1.0) *
                                       multiplier);

            if (oldSampleOffset == viewState.sampleOffset)
            {
                return;
            }

            refreshWaveformsAfterViewChange(state, true, true);
        }
        else if (event->key.scancode == SDL_SCANCODE_RIGHT)
        {
            const int64_t oldSampleOffset = viewState.sampleOffset;

            updateSampleOffset(state,
                               viewState.sampleOffset +
                                   std::max(viewState.samplesPerPixel, 1.0) *
                                       multiplier);

            if (oldSampleOffset == viewState.sampleOffset)
            {
                return;
            }

            refreshWaveformsAfterViewChange(state, true, true);
        }
        else if (event->key.scancode == SDL_SCANCODE_O)
        {
            if (primaryModifierHeld)
            {
                actions::showOpenFileDialog(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_N)
        {
            if (primaryModifierHeld)
            {
                actions::showNewFileDialog(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_S)
        {
            if (primaryModifierHeld)
            {
                if (event->key.mod & SDL_KMOD_SHIFT)
                {
                    if (actions::hasActiveDocument(state))
                    {
                        actions::showExportAudioDialog(state);
                    }
                }
                else
                {
                    actions::overwrite(state);
                }
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_SPACE)
        {
            if (state->audioDevices->isPlaying())
            {
                actions::requestStop(state);
                return;
            }

            actions::play(state);
        }
        else if (event->key.scancode == SDL_SCANCODE_PERIOD &&
                 event->key.mod & SDL_KMOD_SHIFT)
        {
            if (state->pixelScale < 4)
            {
                state->pixelScale = std::min<uint8_t>(state->pixelScale * 2, 4);

                viewState.samplesPerPixel *= 2.0;

                buildComponents(state, mainWindow);
                for (auto *window : state->windows)
                {
                    if (window && window != mainWindow)
                    {
                        window->refreshForScaleOrResize();
                    }
                }

                updateWaveforms(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_COMMA &&
                 event->key.mod & SDL_KMOD_SHIFT)
        {
            if (state->pixelScale > 1)
            {
                state->pixelScale = std::max<uint8_t>(state->pixelScale / 2, 1);

                viewState.samplesPerPixel /= 2.0;

                buildComponents(state, mainWindow);
                for (auto *window : state->windows)
                {
                    if (window && window != mainWindow)
                    {
                        window->refreshForScaleOrResize();
                    }
                }

                updateWaveforms(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_X)
        {
            if (primaryModifierHeld &&
                state->getActiveDocumentSession().selection.isActive())
            {
                actions::audio::performCut(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_C)
        {
            if (primaryModifierHeld &&
                state->getActiveDocumentSession().selection.isActive())
            {
                actions::audio::performCopy(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_V &&
                 state->clipboard.getFrameCount() > 0)
        {
            if (primaryModifierHeld)
            {
                actions::audio::performPaste(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_T)
        {
            if (primaryModifierHeld &&
                state->getActiveDocumentSession().selection.isActive())
            {
                actions::audio::performTrim(state);
            }
        }
    }
} // namespace cupuacu::gui
