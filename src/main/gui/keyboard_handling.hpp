#include <SDL3/SDL.h>
#include "../State.hpp"
#include "audio/AudioDevice.hpp"
#include "audio/AudioDevices.hpp"
#include "Waveform.hpp"
#include "Window.hpp"
#include "../actions/ShowOpenFileDialog.hpp"
#include "../actions/Play.hpp"
#include "../actions/Zoom.hpp"
#include "../actions/Save.hpp"
#include "../actions/audio/Copy.hpp"
#include "../actions/audio/Cut.hpp"
#include "../actions/audio/Paste.hpp"
#include "../actions/audio/Trim.hpp"

namespace cupuacu::gui
{

    static void updateWaveforms(State *state)
    {
        for (const auto waveform : state->waveforms)
        {
            waveform->setDirty();
            waveform->updateSamplePoints();
        }
    }

    static void handleKeyDown(SDL_Event *event, State *state)
    {
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

        if (event->key.scancode == SDL_SCANCODE_Q)
        {
            if (actions::tryZoomOutHorizontally(state))
            {
                updateWaveforms(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_W)
        {
            if (actions::tryZoomInHorizontally(state))
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
#if __APPLE__
            if (event->key.mod & SDL_KMOD_GUI)
#else
            if (event->key.mod & SDL_KMOD_CTRL)
#endif
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
            if (state->sampleOffset == 0)
            {
                return;
            }

            const int64_t oldSampleOffset = state->sampleOffset;

            updateSampleOffset(
                state, state->sampleOffset -
                           std::max(state->samplesPerPixel, 1.0) * multiplier);

            if (oldSampleOffset == state->sampleOffset)
            {
                return;
            }

            resetSampleValueUnderMouseCursor(state);

            for (const auto w : state->waveforms)
            {
                w->clearHighlight();
            }

            updateWaveforms(state);
        }
        else if (event->key.scancode == SDL_SCANCODE_RIGHT)
        {
            const int64_t oldSampleOffset = state->sampleOffset;

            updateSampleOffset(
                state, state->sampleOffset +
                           std::max(state->samplesPerPixel, 1.0) * multiplier);

            if (oldSampleOffset == state->sampleOffset)
            {
                return;
            }

            resetSampleValueUnderMouseCursor(state);

            for (const auto w : state->waveforms)
            {
                w->clearHighlight();
            }

            updateWaveforms(state);
        }
        else if (event->key.scancode == SDL_SCANCODE_O)
        {
#if __APPLE__
            if (event->key.mod & SDL_KMOD_GUI)
#else
            if (event->key.mod & SDL_KMOD_CTRL)
#endif
            {
                actions::showOpenFileDialog(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_S)
        {
#if __APPLE__
            if (event->key.mod & SDL_KMOD_GUI)
#else
            if (event->key.mod & SDL_KMOD_CTRL)
#endif
            {
                actions::overwrite(state);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_SPACE)
        {
            if (state->audioDevices->getOutputDevice()->isPlaying())
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

                const double newSamplesPerPixel = state->samplesPerPixel * 2;

                buildComponents(state, state->mainWindow.get());
                for (auto *window : state->windows)
                {
                    if (window && window != state->mainWindow.get())
                    {
                        window->refreshForScaleOrResize();
                    }
                }

                state->samplesPerPixel = newSamplesPerPixel;

                for (const auto &w : state->waveforms)
                {
                    w->setDirty();
                }
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_COMMA &&
                 event->key.mod & SDL_KMOD_SHIFT)
        {
            if (state->pixelScale > 1)
            {
                state->pixelScale = std::max<uint8_t>(state->pixelScale / 2, 1);

                const double newSamplesPerPixel = state->samplesPerPixel / 2;

                buildComponents(state, state->mainWindow.get());
                for (auto *window : state->windows)
                {
                    if (window && window != state->mainWindow.get())
                    {
                        window->refreshForScaleOrResize();
                    }
                }

                state->samplesPerPixel = newSamplesPerPixel;

                for (const auto &w : state->waveforms)
                {
                    w->setDirty();
                }
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_X)
        {
#if __APPLE__
            if (event->key.mod & SDL_KMOD_GUI && state->selection.isActive())
#else
            if ((event->key.mod & SDL_KMOD_CTRL) && state->selection.isActive())
#endif
            {
                const auto undoable = std::make_shared<actions::audio::Cut>(
                    state, state->selection.getStartInt(),
                    state->selection.getLengthInt());
                state->addAndDoUndoable(undoable);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_C)
        {
#if __APPLE__
            if (event->key.mod & SDL_KMOD_GUI && state->selection.isActive())
#else
            if ((event->key.mod & SDL_KMOD_CTRL) && state->selection.isActive())
#endif
            {
                const auto undoable = std::make_shared<actions::audio::Copy>(
                    state, state->selection.getStartInt(),
                    state->selection.getLengthInt());
                state->addAndDoUndoable(undoable);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_V &&
                 state->clipboard.getFrameCount() > 0)
        {
#if __APPLE__
            if (event->key.mod & SDL_KMOD_GUI)
#else
            if (event->key.mod & SDL_KMOD_CTRL)
#endif
            {
                const int64_t start = state->selection.isActive()
                                          ? state->selection.getStartInt()
                                          : state->cursor;
                const int64_t end = state->selection.isActive()
                                        ? state->selection.getEndInt()
                                        : -1;

                const auto undoable =
                    std::make_shared<actions::audio::Paste>(state, start, end);
                state->addAndDoUndoable(undoable);
            }
        }
        else if (event->key.scancode == SDL_SCANCODE_T)
        {
#if __APPLE__
            if (event->key.mod & SDL_KMOD_GUI && state->selection.isActive())
#else
            if ((event->key.mod & SDL_KMOD_CTRL) && state->selection.isActive())
#endif
            {
                const auto undoable = std::make_shared<actions::audio::Trim>(
                    state, state->selection.getStartInt(),
                    state->selection.getLengthInt());
                state->addAndDoUndoable(undoable);
            }
        }
    }
} // namespace cupuacu::gui
