#include "State.hpp"

#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"

#include "gui/Waveform.hpp"
#include "gui/DisplaySettingsWindow.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/ExportAudioDialogWindow.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"
#include "gui/NewFileDialogWindow.hpp"
#include "actions/Undoable.hpp"

int64_t getMaxSampleOffset(const cupuacu::State *state)
{
    if (!state->mainDocumentSessionWindow || state->waveforms.empty() ||
        state->getActiveDocumentSession().document.getFrameCount() == 0)
    {
        return 0;
    }
    const auto &viewState = state->getActiveViewState();

    const double waveformWidth =
        static_cast<double>(state->waveforms.front()->getWidth());
    const int64_t visibleSampleCount = static_cast<int64_t>(
        std::ceil(waveformWidth * viewState.samplesPerPixel));
    const int64_t frameCount = state->getActiveDocumentSession().document.getFrameCount();
    const int64_t maxOffset = frameCount - visibleSampleCount;
    return std::max<int64_t>(0, maxOffset);
}

void cupuacu::destroyAmplifyFadeDialog(effects::AmplifyFadeDialog *dialog)
{
    delete dialog;
}

void cupuacu::destroyDynamicsDialog(effects::DynamicsDialog *dialog)
{
    delete dialog;
}

void cupuacu::destroyRemoveSilenceDialog(effects::RemoveSilenceDialog *dialog)
{
    delete dialog;
}

void cupuacu::destroyDisplaySettingsWindow(gui::DisplaySettingsWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyNewFileDialogWindow(gui::NewFileDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyGenerateSilenceDialogWindow(
    gui::GenerateSilenceDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyExportAudioDialogWindow(gui::ExportAudioDialogWindow *dialog)
{
    delete dialog;
}

cupuacu::State::~State() = default;

void cupuacu::State::addUndoable(
    std::shared_ptr<cupuacu::actions::Undoable> undoable)
{
    auto &undoables = getActiveUndoables();
    auto &redoables = getActiveRedoables();
    undoables.push_back(undoable);
    redoables.clear();
}

void cupuacu::State::addAndDoUndoable(
    std::shared_ptr<cupuacu::actions::Undoable> undoable)
{
    addUndoable(undoable);
    undoable->redo();
    undoable->updateGui();
}

void cupuacu::State::undo()
{
    auto &undoables = getActiveUndoables();
    auto &redoables = getActiveRedoables();
    if (undoables.empty())
    {
        return;
    }

    auto undoable = undoables.back();
    undoables.pop_back();
    undoable->undo();
    undoable->updateGui();
    redoables.push_back(undoable);
}

void cupuacu::State::redo()
{
    auto &undoables = getActiveUndoables();
    auto &redoables = getActiveRedoables();
    if (redoables.empty())
    {
        return;
    }

    auto redoable = redoables.back();
    redoables.pop_back();
    redoable->redo();
    redoable->updateGui();
    undoables.push_back(redoable);
}

std::string cupuacu::State::getUndoDescription()
{
    if (!canUndo())
    {
        return "";
    }
    return getActiveUndoables().back()->getUndoDescription();
}
std::string cupuacu::State::getRedoDescription()
{
    if (!canRedo())
    {
        return "";
    }
    return getActiveRedoables().back()->getRedoDescription();
}
