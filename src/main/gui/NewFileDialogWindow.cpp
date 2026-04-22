#include "NewFileDialogWindow.hpp"

#include "../State.hpp"
#include "../actions/DocumentLifecycle.hpp"

#include "Colors.hpp"
#include "SecondaryWindowLifecycle.hpp"
#include "text.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace
{
    constexpr int kWindowWidth = 420;
    constexpr int kWindowHeight = 280;

    constexpr Uint32 getHighDensityWindowFlag()
    {
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
} // namespace

namespace cupuacu::gui
{
    NewFileDialogWindow::NewFileDialogWindow(State *stateToUse) : state(stateToUse)
    {
        if (!state)
        {
            return;
        }

        window = std::make_unique<Window>(
            state, "New File", kWindowWidth, kWindowHeight,
            getHighDensityWindowFlag());
        if (!window || !window->isOpen())
        {
            return;
        }

        attachSecondaryWindow(state, window.get(), true);

        auto root = std::make_unique<Component>(state, "NewFileDialogRoot");
        root->setVisible(true);

        background = root->emplaceChild<OpaqueRect>(state, Colors::background);
        sampleRateLabel = root->emplaceChild<Label>(state, "Sample rate");
        bitDepthLabel = root->emplaceChild<Label>(state, "Bit depth");
        channelCountLabel = root->emplaceChild<Label>(state, "Channels");
        sampleRateDropdown = root->emplaceChild<DropdownMenu>(state);
        bitDepthDropdown = root->emplaceChild<DropdownMenu>(state);
        channelCountDropdown = root->emplaceChild<DropdownMenu>(state);
        cancelButton = root->emplaceChild<TextButton>(state, "Cancel");
        okButton = root->emplaceChild<TextButton>(state, "OK");

        const int labelFontSize = state->menuFontSize;
        sampleRateLabel->setFontSize(labelFontSize);
        bitDepthLabel->setFontSize(labelFontSize);
        channelCountLabel->setFontSize(labelFontSize);
        sampleRateDropdown->setFontSize(labelFontSize);
        bitDepthDropdown->setFontSize(labelFontSize);
        channelCountDropdown->setFontSize(labelFontSize);
        sampleRateDropdown->setItems({"11025", "22050", "44100", "48000", "96000"});
        bitDepthDropdown->setItems({"8 bit", "16 bit"});
        channelCountDropdown->setItems({"1", "2"});
        sampleRateDropdown->setSelectedIndex(2);
        bitDepthDropdown->setSelectedIndex(1);
        channelCountDropdown->setSelectedIndex(1);
        cancelButton->setTriggerOnMouseUp(true);
        okButton->setTriggerOnMouseUp(true);
        const auto applyNewFile = [this]()
        {
            cupuacu::actions::createNewDocumentInNewTab(
                state, selectedSampleRate(), selectedFormat(),
                selectedChannelCount());
            requestClose();
        };
        cancelButton->setOnPress([this]() { requestClose(); });
        okButton->setOnPress(applyNewFile);

        window->setOnResize([this]() { layoutComponents(); });
        window->setDefaultAction(applyNewFile);
        window->setCancelAction([this]() { requestClose(); });
        window->setOnClose([this]() { detachFromState(); });
        window->setRootComponent(std::move(root));
        layoutComponents();
        window->renderFrame();
    }

    NewFileDialogWindow::~NewFileDialogWindow()
    {
        detachFromState();
    }

    bool NewFileDialogWindow::isOpen() const
    {
        return window && window->isOpen();
    }

    void NewFileDialogWindow::raise() const
    {
        raiseSecondaryWindow(window.get());
    }

    void NewFileDialogWindow::requestClose()
    {
        if (!window || !window->isOpen())
        {
            detachFromState();
            return;
        }

        requestSecondaryWindowClose(state, window.get());
    }

    void NewFileDialogWindow::detachFromState()
    {
        if (!state)
        {
            return;
        }

        detachSecondaryWindow(state, window.get());
    }

    void NewFileDialogWindow::layoutComponents() const
    {
        if (!window || !window->getRootComponent())
        {
            return;
        }

        float canvasW = 0.0f;
        float canvasH = 0.0f;
        SDL_GetTextureSize(window->getCanvas(), &canvasW, &canvasH);
        const int width = static_cast<int>(canvasW);
        const int height = static_cast<int>(canvasH);
        const int padding = scaleUi(state, 20.0f);
        const uint8_t labelFontPointSize =
            scaleFontPointSize(state, state->menuFontSize);
        const auto [sampleRateTextWidth, _sampleRateTextHeight] =
            measureText("Sample rate", labelFontPointSize);
        const auto [bitDepthTextWidth, _bitDepthTextHeight] =
            measureText("Bit depth", labelFontPointSize);
        const auto [channelCountTextWidth, _channelCountTextHeight] =
            measureText("Channels", labelFontPointSize);
        const int labelWidth = std::max(
            {sampleRateTextWidth, bitDepthTextWidth, channelCountTextWidth}) +
                               scaleUi(state, 20.0f);
        const int rowHeight = scaleUi(state, 42.0f);
        const int buttonWidth = scaleUi(state, 100.0f);
        const int buttonHeight = scaleUi(state, 40.0f);
        const int fieldX = padding + labelWidth;
        const int fieldWidth = width - fieldX - padding;

        window->getRootComponent()->setBounds(0, 0, width, height);
        background->setBounds(0, 0, width, height);

        sampleRateLabel->setBounds(padding, padding, labelWidth, rowHeight);
        sampleRateDropdown->setBounds(fieldX, padding, fieldWidth, rowHeight);
        bitDepthLabel->setBounds(padding, padding + rowHeight + padding,
                                 labelWidth, rowHeight);
        bitDepthDropdown->setBounds(fieldX, padding + rowHeight + padding,
                                    fieldWidth, rowHeight);
        channelCountLabel->setBounds(padding, padding + (rowHeight + padding) * 2,
                                     labelWidth, rowHeight);
        channelCountDropdown->setBounds(
            fieldX, padding + (rowHeight + padding) * 2, fieldWidth, rowHeight);

        const int buttonY = height - padding - buttonHeight;
        cancelButton->setBounds(width - padding - buttonWidth * 2 - padding,
                                buttonY, buttonWidth, buttonHeight);
        okButton->setBounds(width - padding - buttonWidth, buttonY, buttonWidth,
                            buttonHeight);
    }

    int NewFileDialogWindow::selectedSampleRate() const
    {
        switch (sampleRateDropdown ? sampleRateDropdown->getSelectedIndex() : -1)
        {
            case 0:
                return 11025;
            case 1:
                return 22050;
            case 2:
                return 44100;
            case 3:
                return 48000;
            case 4:
                return 96000;
            default:
                return 44100;
        }
    }

    cupuacu::SampleFormat NewFileDialogWindow::selectedFormat() const
    {
        return (bitDepthDropdown && bitDepthDropdown->getSelectedIndex() == 0)
                   ? cupuacu::SampleFormat::PCM_S8
                   : cupuacu::SampleFormat::PCM_S16;
    }

    int NewFileDialogWindow::selectedChannelCount() const
    {
        return (channelCountDropdown &&
                channelCountDropdown->getSelectedIndex() == 0)
                   ? 1
                   : 2;
    }
} // namespace cupuacu::gui
