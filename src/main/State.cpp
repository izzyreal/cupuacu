#include "State.hpp"

#include "effects/AmplifyEnvelopeEffect.hpp"
#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"

#include "gui/Waveform.hpp"
#include "gui/ExportAudioDialogWindow.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"
#include "gui/MarkerEditorDialogWindow.hpp"
#include "gui/NewFileDialogWindow.hpp"
#include "gui/OptionsWindow.hpp"
#include "actions/BackgroundOpen.hpp"
#include "actions/Undoable.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "file/OverwritePreservation.hpp"
#include "file/OverwritePreservationMutation.hpp"

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
    const int64_t frameCount =
        state->getActiveDocumentSession().document.getFrameCount();
    const int64_t maxOffset = frameCount - visibleSampleCount;
    return std::max<int64_t>(0, maxOffset);
}

void cupuacu::destroyAmplifyEnvelopeDialog(
    effects::AmplifyEnvelopeDialog *dialog)
{
    delete dialog;
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

void cupuacu::destroyOptionsWindow(gui::OptionsWindow *window)
{
    delete window;
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

void cupuacu::destroyExportAudioDialogWindow(
    gui::ExportAudioDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyMarkerEditorDialogWindow(
    gui::MarkerEditorDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyBackgroundOpenJob(actions::BackgroundOpenJob *job)
{
    delete job;
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
    cupuacu::file::OverwritePreservationMutationHelper::applyToSession(
        getActiveDocumentSession(), undoable->overwritePreservationMutation());
    cupuacu::file::OverwritePreservation::refreshActiveSession(this);
    undoable->updateGui();
    cupuacu::actions::autosaveActiveDocumentAfterMutation(this);
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
    cupuacu::file::OverwritePreservationMutationHelper::revertOnSession(
        getActiveDocumentSession(), undoable->overwritePreservationMutation());
    cupuacu::file::OverwritePreservation::refreshActiveSession(this);
    undoable->updateGui();
    redoables.push_back(undoable);
    cupuacu::actions::autosaveActiveDocumentAfterMutation(this);
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
    cupuacu::file::OverwritePreservationMutationHelper::applyToSession(
        getActiveDocumentSession(), redoable->overwritePreservationMutation());
    cupuacu::file::OverwritePreservation::refreshActiveSession(this);
    redoable->updateGui();
    undoables.push_back(redoable);
    cupuacu::actions::autosaveActiveDocumentAfterMutation(this);
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
