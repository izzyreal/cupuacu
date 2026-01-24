#pragma once

#include <SDL3/SDL.h>

#include "gui/Selection.h"
#include "Document.h"

#include <cstdint>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <optional>
#include <deque>

namespace cupuacu
{
    namespace actions
    {
        struct CustomDataSource;
        class Undoable;
    } // namespace actions

    namespace gui
    {
        class MenuBar;
        class Component;
        class Waveform;
        class MainView;
        class VuMeter;
        class VuMeterContainer;
    } // namespace gui

    enum SelectedChannels
    {
        BOTH,
        LEFT,
        RIGHT
    };

    struct State
    {
        std::deque<std::shared_ptr<actions::Undoable>> undoables;
        std::deque<std::shared_ptr<actions::Undoable>> redoables;
        uint8_t menuFontSize = 40;
        uint8_t pixelScale = 1;
        std::string currentFile =
            "/Users/izmar/Documents/VMPC2000XL/Volumes/MPC2000XL.bk2/BOAT.WAV";
        Document document;
        Document clipboard;

        std::vector<SDL_Rect> dirtyRects;

        double samplesPerPixel = 1;
        double verticalZoom;
        int64_t sampleOffset;
        gui::Selection<double> selection = gui::Selection<double>(0.0);
        SelectedChannels selectedChannels;
        SelectedChannels hoveringOverChannels;
        double samplesToScroll;
        std::optional<float> sampleValueUnderMouseCursor;

        int64_t cursor = 0;
        std::atomic<int64_t> playbackPosition;
        std::atomic<bool> isPlaying = false;

        std::shared_ptr<actions::CustomDataSource> activePlayback;

        SDL_Window *window = NULL;
        SDL_Renderer *renderer = NULL;
        SDL_Texture *canvas = NULL;
        SDL_Texture *textTexture = NULL;

        gui::Component *capturingComponent = nullptr;
        gui::Component *componentUnderMouse = nullptr;

        std::vector<gui::Waveform *> waveforms;
        std::unique_ptr<gui::Component> rootComponent;
        gui::MenuBar *menuBar;
        gui::MainView *mainView;
        gui::Component *statusBar;
        gui::VuMeterContainer *vuMeterContainer;
        gui::VuMeter *vuMeter;

        void addUndoable(std::shared_ptr<actions::Undoable>);
        void addAndDoUndoable(std::shared_ptr<actions::Undoable>);
        void undo();
        void redo();
        bool canUndo()
        {
            return !undoables.empty();
        }
        bool canRedo()
        {
            return !redoables.empty();
        }
        std::string getUndoDescription();
        std::string getRedoDescription();
    };
} // namespace cupuacu

static void resetSampleValueUnderMouseCursor(cupuacu::State *state)
{
    state->sampleValueUnderMouseCursor.reset();
}

static void updateSampleValueUnderMouseCursor(cupuacu::State *state,
                                              const float sampleValue)
{
    state->sampleValueUnderMouseCursor.emplace(sampleValue);
}

static void resetWaveformState(cupuacu::State *state)
{
    state->verticalZoom = 1;
    state->sampleOffset = 0;
    state->selection.reset();
    state->selectedChannels = cupuacu::SelectedChannels::BOTH;
    state->samplesToScroll = 0;
    state->playbackPosition.store(-1);
}

int64_t getMaxSampleOffset(const cupuacu::State *);

static void updateSampleOffset(cupuacu::State *state,
                               const int64_t sampleOffset)
{
    state->sampleOffset =
        std::clamp(sampleOffset, int64_t{0}, getMaxSampleOffset(state));
}

static bool updateCursorPos(cupuacu::State *state, const int64_t cursorPos)
{
    const int64_t oldCursor = state->cursor;
    state->cursor =
        std::clamp(cursorPos, int64_t{0}, state->document.getFrameCount());
    return state->cursor != oldCursor;
}
