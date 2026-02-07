#pragma once

#include <SDL3/SDL.h>

#include "gui/Selection.hpp"

#include "SelectedChannels.hpp"
#include "Document.hpp"
#include "Paths.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <deque>

namespace cupuacu
{
    namespace audio
    {
        class AudioDevices;
    } // namespace audio

    namespace actions
    {
        struct CustomDataSource;
        class Undoable;
    } // namespace actions

    namespace gui
    {
        class DevicePropertiesWindow;
        class Window;
        class Component;
        class Waveform;
        class MainView;
        class VuMeter;
        class VuMeterContainer;
    } // namespace gui

    struct State
    {
        std::shared_ptr<audio::AudioDevices> audioDevices;
        std::unique_ptr<Paths> paths = std::make_unique<Paths>();
        std::deque<std::shared_ptr<actions::Undoable>> undoables;
        std::deque<std::shared_ptr<actions::Undoable>> redoables;
        uint8_t menuFontSize = 40;
        uint8_t pixelScale = 1;
        std::string currentFile =
            "/Users/izmar/Documents/VMPC2000XL/Volumes/MPC2000XL.bk2/BOAT.WAV";
        Document document;
        Document clipboard;

        double samplesPerPixel = 1;
        double verticalZoom;
        int64_t sampleOffset;
        gui::Selection<double> selection = gui::Selection<double>(0.0);
        SelectedChannels selectedChannels;
        SelectedChannels hoveringOverChannels;
        double samplesToScroll;
        std::optional<float> sampleValueUnderMouseCursor;

        int64_t cursor = 0;

        std::vector<gui::Waveform *> waveforms;
        std::vector<gui::Window *> windows;
        std::unique_ptr<gui::Window> mainWindow;
        std::unique_ptr<gui::DevicePropertiesWindow> devicePropertiesWindow;
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
