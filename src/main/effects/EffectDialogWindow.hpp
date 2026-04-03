#pragma once

#include "EffectTargeting.hpp"

#include "audio/AudioDevices.hpp"
#include "gui/Colors.hpp"
#include "gui/Component.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/Label.hpp"
#include "gui/OpaqueRect.hpp"
#include "gui/Slider.hpp"
#include "gui/TextButton.hpp"
#include "gui/TextInput.hpp"
#include "gui/UiScale.hpp"
#include "gui/VuMeterAccess.hpp"
#include "gui/Window.hpp"
#include "gui/text.hpp"
#include "audio/AudioMessage.hpp"

#include "actions/Play.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cupuacu::effects
{
    enum class EffectParameterKind
    {
        Percent,
        Number,
        Enum,
        Toggle,
        Action
    };

    template <typename Settings>
    struct EffectParameterSpec
    {
        std::string id;
        std::string label;
        EffectParameterKind kind = EffectParameterKind::Percent;
        double minValue = 0.0;
        double maxValue = 0.0;
        std::vector<std::string> enumItems;
        std::string buttonText;
        std::function<double(const Settings &)> getPercent;
        std::function<void(Settings &, double)> setPercent;
        std::function<std::string(cupuacu::State *, const Settings &)> formatNumber;
        std::function<bool(cupuacu::State *, Settings &, const std::string &)>
            parseNumber;
        std::string numberAllowedCharacters;
        std::function<int(const Settings &)> getEnum;
        std::function<void(Settings &, int)> setEnum;
        std::function<bool(const Settings &)> getToggle;
        std::function<void(Settings &, bool)> setToggle;
        std::function<void(Settings &, cupuacu::State *)> invokeAction;

        static EffectParameterSpec percent(
            std::string idToUse, std::string labelToUse, const double minToUse,
            const double maxToUse,
            std::function<double(const Settings &)> getter,
            std::function<void(Settings &, double)> setter)
        {
            EffectParameterSpec spec{};
            spec.id = std::move(idToUse);
            spec.label = std::move(labelToUse);
            spec.kind = EffectParameterKind::Percent;
            spec.minValue = minToUse;
            spec.maxValue = maxToUse;
            spec.getPercent = std::move(getter);
            spec.setPercent = std::move(setter);
            return spec;
        }

        static EffectParameterSpec enumeration(
            std::string idToUse, std::string labelToUse,
            std::vector<std::string> itemsToUse,
            std::function<int(const Settings &)> getter,
            std::function<void(Settings &, int)> setter)
        {
            EffectParameterSpec spec{};
            spec.id = std::move(idToUse);
            spec.label = std::move(labelToUse);
            spec.kind = EffectParameterKind::Enum;
            spec.enumItems = std::move(itemsToUse);
            spec.getEnum = std::move(getter);
            spec.setEnum = std::move(setter);
            return spec;
        }

        static EffectParameterSpec number(
            std::string idToUse, std::string labelToUse,
            std::function<std::string(cupuacu::State *, const Settings &)> formatter,
            std::function<bool(cupuacu::State *, Settings &, const std::string &)>
                parser,
            std::string allowedCharactersToUse = "0123456789.-")
        {
            EffectParameterSpec spec{};
            spec.id = std::move(idToUse);
            spec.label = std::move(labelToUse);
            spec.kind = EffectParameterKind::Number;
            spec.formatNumber = std::move(formatter);
            spec.parseNumber = std::move(parser);
            spec.numberAllowedCharacters = std::move(allowedCharactersToUse);
            return spec;
        }

        static EffectParameterSpec toggle(
            std::string idToUse, std::string buttonTextToUse,
            std::function<bool(const Settings &)> getter,
            std::function<void(Settings &, bool)> setter)
        {
            EffectParameterSpec spec{};
            spec.id = std::move(idToUse);
            spec.kind = EffectParameterKind::Toggle;
            spec.buttonText = std::move(buttonTextToUse);
            spec.getToggle = std::move(getter);
            spec.setToggle = std::move(setter);
            return spec;
        }

        static EffectParameterSpec action(
            std::string idToUse, std::string buttonTextToUse,
            std::function<void(Settings &, cupuacu::State *)> callback)
        {
            EffectParameterSpec spec{};
            spec.id = std::move(idToUse);
            spec.kind = EffectParameterKind::Action;
            spec.buttonText = std::move(buttonTextToUse);
            spec.invokeAction = std::move(callback);
            return spec;
        }
    };

    template <typename Settings>
    struct EffectActionSpec
    {
        std::string label;
        std::function<void(Settings &, cupuacu::State *)> apply;
    };

    template <typename Settings>
    class EffectPreviewSession
    {
    public:
        virtual ~EffectPreviewSession() = default;
        virtual std::shared_ptr<const cupuacu::audio::AudioProcessor>
        getProcessor() const = 0;
        virtual void updateSettings(const Settings &settings) = 0;
    };

    template <typename Settings>
    class EffectPreviewPanel
    {
    public:
        virtual ~EffectPreviewPanel() = default;
        virtual void build(cupuacu::State *state,
                           cupuacu::gui::Component *root) = 0;
        virtual void sync(const Settings &settings) = 0;
        virtual void layout(const SDL_Rect &bounds) = 0;
    };

    template <typename Settings>
    struct EffectDialogDefinition
    {
        std::string title;
        std::function<Settings(cupuacu::State *)> loadSettings;
        std::function<void(cupuacu::State *, const Settings &)> saveSettings;
        std::function<void(cupuacu::State *, const Settings &)> applySettings;
        std::function<std::shared_ptr<EffectPreviewSession<Settings>>(
            cupuacu::State *, const Settings &)>
            createPreviewSession;
        std::function<std::unique_ptr<EffectPreviewPanel<Settings>>()>
            createPreviewPanel;
        std::vector<EffectParameterSpec<Settings>> parameters;
        std::vector<EffectActionSpec<Settings>> actions;
    };

    template <typename Settings>
    class EffectDialogWindow
    {
    public:
        EffectDialogWindow(cupuacu::State *stateToUse,
                           EffectDialogDefinition<Settings> definitionToUse,
                           const int windowWidthToUse,
                           const int windowHeightToUse)
            : state(stateToUse), definition(std::move(definitionToUse)),
              windowWidth(windowWidthToUse), windowHeight(windowHeightToUse)
        {
            if (!state)
            {
                return;
            }

            settings = definition.loadSettings ? definition.loadSettings(state)
                                               : Settings{};

            window = std::make_unique<cupuacu::gui::Window>(
                state, definition.title, windowWidth, windowHeight,
                SDL_WINDOW_RESIZABLE | getHighDensityWindowFlag());
            if (!window->isOpen())
            {
                return;
            }

            state->windows.push_back(window.get());
            state->modalWindow = window.get();
            if (auto *mainWindow = state->mainDocumentSessionWindow
                                       ? state->mainDocumentSessionWindow
                                             ->getWindow()
                                       : nullptr;
                mainWindow && mainWindow->getSdlWindow())
            {
                SDL_SetWindowParent(window->getSdlWindow(),
                                    mainWindow->getSdlWindow());
            }

            auto rootComponent = std::make_unique<cupuacu::gui::Component>(
                state, definition.title + "Root");
            rootComponent->setVisible(true);
            background = rootComponent->template emplaceChild<cupuacu::gui::OpaqueRect>(
                state, cupuacu::gui::Colors::background);

            buildParameterControls(rootComponent.get());
            buildActionButtons(rootComponent.get());

            if (definition.createPreviewPanel)
            {
                previewPanel = definition.createPreviewPanel();
                if (previewPanel)
                {
                    previewPanel->build(state, rootComponent.get());
                }
            }

            if (definition.createPreviewSession)
            {
                previewButton =
                    rootComponent->template emplaceChild<cupuacu::gui::TextButton>(
                        state, "Preview");
                previewButton->setTriggerOnMouseUp(true);
                previewButton->setOnPress([this]() { togglePreview(); });
            }
            cancelButton = rootComponent->template emplaceChild<cupuacu::gui::TextButton>(
                state, "Cancel");
            applyButton = rootComponent->template emplaceChild<cupuacu::gui::TextButton>(
                state, "Apply");
            cancelButton->setTriggerOnMouseUp(true);
            applyButton->setTriggerOnMouseUp(true);
            cancelButton->setOnPress([this]() { closeNow(); });
            applyButton->setOnPress([this]() { applyAndClose(); });

            window->setOnResize(
                [this]
                {
                    layoutComponents();
                    renderIfDirty();
                });
            window->setDefaultAction([this]() { applyAndClose(); });
            window->setCancelAction([this]() { closeNow(); });
            window->setOnClose(
                [this]
                {
                    if (!window || !state)
                    {
                        return;
                    }
                    const auto it =
                        std::find(state->windows.begin(), state->windows.end(),
                                  window.get());
                    if (it != state->windows.end())
                    {
                        state->windows.erase(it);
                    }
                    if (state->modalWindow == window.get())
                    {
                        state->modalWindow = nullptr;
                    }
                });

            window->setRootComponent(std::move(rootComponent));
            syncAllControls();
            persistSettings();
            layoutComponents();
            window->renderFrame();
        }

        ~EffectDialogWindow()
        {
            if (window && state)
            {
                const auto it = std::find(state->windows.begin(), state->windows.end(),
                                          window.get());
                if (it != state->windows.end())
                {
                    state->windows.erase(it);
                }
                if (state->modalWindow == window.get())
                {
                    state->modalWindow = nullptr;
                }
            }
        }

        bool isOpen() const
        {
            return window && window->isOpen();
        }

        void raise() const
        {
            if (window && window->getSdlWindow())
            {
                SDL_RaiseWindow(window->getSdlWindow());
            }
        }

        cupuacu::gui::Window *getWindow() const
        {
            return window.get();
        }

        const Settings &getSettings() const
        {
            return settings;
        }

    private:
        struct ParameterControl
        {
            std::size_t specIndex = 0;
            cupuacu::gui::Label *label = nullptr;
            cupuacu::gui::TextInput *input = nullptr;
            cupuacu::gui::Slider *slider = nullptr;
            cupuacu::gui::DropdownMenu *dropdown = nullptr;
            cupuacu::gui::TextButton *toggleButton = nullptr;
        };

        cupuacu::State *state = nullptr;
        EffectDialogDefinition<Settings> definition;
        Settings settings{};
        std::unique_ptr<cupuacu::gui::Window> window;
        cupuacu::gui::OpaqueRect *background = nullptr;
        std::vector<ParameterControl> controls;
        std::vector<cupuacu::gui::TextButton *> actionButtons;
        std::unique_ptr<EffectPreviewPanel<Settings>> previewPanel;
        cupuacu::gui::TextButton *previewButton = nullptr;
        cupuacu::gui::TextButton *cancelButton = nullptr;
        cupuacu::gui::TextButton *applyButton = nullptr;
        int windowWidth = 560;
        int windowHeight = 320;
        bool syncingControls = false;
        bool previewStartedByDialog = false;
        std::shared_ptr<EffectPreviewSession<Settings>> previewSession;

        static constexpr Uint32 getHighDensityWindowFlag()
        {
#if defined(__linux__)
            return 0;
#else
            return SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
        }

        static std::string formatPercent(const double value)
        {
            std::ostringstream stream;
            const double rounded = std::round(value);
            if (std::fabs(value - rounded) < 1e-9)
            {
                stream << static_cast<long long>(std::llround(rounded));
            }
            else
            {
                stream << std::fixed << std::setprecision(2) << value;
                auto formatted = stream.str();
                while (!formatted.empty() && formatted.back() == '0')
                {
                    formatted.pop_back();
                }
                if (!formatted.empty() && formatted.back() == '.')
                {
                    formatted.pop_back();
                }
                return formatted + "%";
            }
            return stream.str() + "%";
        }

        static bool tryParsePercent(const std::string &text, double &valueOut)
        {
            std::string sanitized;
            sanitized.reserve(text.size());
            for (const char c : text)
            {
                if (c != '%')
                {
                    sanitized.push_back(c);
                }
            }

            if (sanitized.empty())
            {
                return false;
            }

            try
            {
                std::size_t consumed = 0;
                const double parsed = std::stod(sanitized, &consumed);
                if (consumed != sanitized.size() || !std::isfinite(parsed))
                {
                    return false;
                }
                valueOut = parsed;
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        void buildParameterControls(cupuacu::gui::Component *root)
        {
            const int labelFontSize = static_cast<int>(state->menuFontSize);
            controls.reserve(definition.parameters.size());
            for (std::size_t index = 0; index < definition.parameters.size(); ++index)
            {
                const auto &spec = definition.parameters[index];
                ParameterControl control{};
                control.specIndex = index;

                switch (spec.kind)
                {
                case EffectParameterKind::Percent:
                case EffectParameterKind::Number:
                    control.label = root->template emplaceChild<cupuacu::gui::Label>(
                        state, spec.label);
                    control.input = root->template emplaceChild<cupuacu::gui::TextInput>(
                        state);
                    control.label->setFontSize(labelFontSize);
                    control.input->setFontSize(labelFontSize - 6);
                    if (spec.kind == EffectParameterKind::Percent)
                    {
                        control.slider =
                            root->template emplaceChild<cupuacu::gui::Slider>(
                                state,
                                [this, index]()
                                {
                                    return definition.parameters[index].getPercent(
                                        settings);
                                },
                                [this, index]()
                                {
                                    return definition.parameters[index].minValue;
                                },
                                [this, index]()
                                {
                                    return definition.parameters[index].maxValue;
                                },
                                [this, index](const double value)
                                {
                                    updateSettings(
                                        [&, value]
                                        {
                                            definition.parameters[index].setPercent(
                                                settings, value);
                                        },
                                        true);
                                });
                        control.input->setAllowedCharacters("0123456789.%");
                        control.input->setOnTextChanged(
                            [this, index](const std::string &text)
                            {
                                if (syncingControls)
                                {
                                    return;
                                }
                                double parsed = 0.0;
                                if (tryParsePercent(text, parsed))
                                {
                                    updateSettings(
                                        [&, parsed]
                                        {
                                            definition.parameters[index].setPercent(
                                                settings, parsed);
                                        },
                                        false);
                                }
                            });
                        control.input->setOnEditingFinished(
                            [this, index](const std::string &text)
                            {
                                if (syncingControls)
                                {
                                    return;
                                }
                                double parsed = 0.0;
                                if (tryParsePercent(text, parsed))
                                {
                                    updateSettings(
                                        [&, parsed]
                                        {
                                            definition.parameters[index].setPercent(
                                                settings, parsed);
                                        },
                                        true);
                                }
                                else
                                {
                                    syncAllControls();
                                    renderIfDirty();
                                }
                            });
                    }
                    else
                    {
                        control.input->setAllowedCharacters(
                            spec.numberAllowedCharacters);
                        control.input->setSubmitOnFocusLost(true);
                        control.input->setConsumeEnterKey(true);
                        control.input->setOnEditingFinished(
                            [this, index](const std::string &text)
                            {
                                if (syncingControls)
                                {
                                    return;
                                }
                                const bool parsed = definition.parameters[index]
                                                        .parseNumber(state, settings,
                                                                     text);
                                if (parsed)
                                {
                                    persistSettings();
                                    if (previewSession)
                                    {
                                        previewSession->updateSettings(settings);
                                    }
                                    syncAllControls();
                                    renderIfDirty();
                                }
                                if (!parsed)
                                {
                                    syncAllControls();
                                    renderIfDirty();
                                }
                            });
                        control.input->setOnEditingCanceled(
                            [this]()
                            {
                                syncAllControls();
                                renderIfDirty();
                            });
                    }
                    break;
                case EffectParameterKind::Enum:
                    control.label = root->template emplaceChild<cupuacu::gui::Label>(
                        state, spec.label);
                    control.dropdown =
                        root->template emplaceChild<cupuacu::gui::DropdownMenu>(state);
                    control.label->setFontSize(labelFontSize);
                    control.dropdown->setFontSize(labelFontSize);
                    control.dropdown->setItems(spec.enumItems);
                    control.dropdown->setOnSelectionChanged(
                        [this, index](const int selection)
                        {
                            if (syncingControls)
                            {
                                return;
                            }
                            updateSettings(
                                [&, selection]
                                {
                                    definition.parameters[index].setEnum(
                                        settings, selection);
                                },
                                true);
                        });
                    break;
                case EffectParameterKind::Toggle:
                    control.toggleButton =
                        root->template emplaceChild<cupuacu::gui::TextButton>(
                            state, spec.buttonText,
                            cupuacu::gui::ButtonType::Toggle);
                    control.toggleButton->setOnToggle(
                        [this, index](const bool enabled)
                        {
                            if (syncingControls)
                            {
                                return;
                            }
                            updateSettings(
                                [&, enabled]
                                {
                                    definition.parameters[index].setToggle(
                                        settings, enabled);
                                },
                                true);
                    });
                    break;
                case EffectParameterKind::Action:
                    control.toggleButton =
                        root->template emplaceChild<cupuacu::gui::TextButton>(
                            state, spec.buttonText);
                    control.toggleButton->setTriggerOnMouseUp(true);
                    control.toggleButton->setOnPress(
                        [this, index]()
                        {
                            if (syncingControls)
                            {
                                return;
                            }
                            definition.parameters[index].invokeAction(settings, state);
                            persistSettings();
                            if (previewSession)
                            {
                                previewSession->updateSettings(settings);
                            }
                            syncAllControls();
                            renderIfDirty();
                        });
                    break;
                }

                controls.push_back(control);
            }
        }

        void buildActionButtons(cupuacu::gui::Component *root)
        {
            actionButtons.reserve(definition.actions.size());
            for (const auto &action : definition.actions)
            {
                auto *button = root->template emplaceChild<cupuacu::gui::TextButton>(
                    state, action.label);
                button->setOnPress(
                    [this, callback = action.apply]()
                    {
                        updateSettings(
                            [&, callback]
                            {
                                callback(settings, state);
                            },
                            true);
                    });
                actionButtons.push_back(button);
            }
        }

        void persistSettings() const
        {
            if (definition.saveSettings)
            {
                definition.saveSettings(state, settings);
            }
        }

        void syncAllControls()
        {
            syncingControls = true;
            for (const auto &control : controls)
            {
                const auto &spec = definition.parameters[control.specIndex];
                switch (spec.kind)
                {
                case EffectParameterKind::Percent:
                    if (control.input)
                    {
                        control.input->setText(
                            formatPercent(spec.getPercent(settings)));
                    }
                    if (control.slider)
                    {
                        control.slider->setDirty();
                    }
                    break;
                case EffectParameterKind::Number:
                    if (control.input)
                    {
                        control.input->setText(
                            spec.formatNumber ? spec.formatNumber(state, settings)
                                              : std::string{});
                    }
                    break;
                case EffectParameterKind::Enum:
                    if (control.dropdown)
                    {
                        control.dropdown->setSelectedIndex(
                            spec.getEnum(settings));
                    }
                    break;
                case EffectParameterKind::Toggle:
                case EffectParameterKind::Action:
                    if (control.toggleButton)
                    {
                        control.toggleButton->setText(spec.buttonText);
                        if (spec.kind == EffectParameterKind::Toggle)
                        {
                            control.toggleButton->setToggled(spec.getToggle(settings));
                        }
                    }
                    break;
                }
            }
            if (previewPanel)
            {
                previewPanel->sync(settings);
            }
            syncingControls = false;
        }

        void updateSettings(const std::function<void()> &mutation,
                            const bool shouldRender)
        {
            mutation();
            persistSettings();
            if (previewSession)
            {
                previewSession->updateSettings(settings);
            }
            syncAllControls();
            if (shouldRender)
            {
                renderIfDirty();
            }
        }

        void renderIfDirty() const
        {
            if (window)
            {
                window->renderFrameIfDirty();
            }
        }

        void applyAndClose()
        {
            stopPreview();
            if (definition.applySettings)
            {
                definition.applySettings(state, settings);
            }
            closeNow();
        }

        void closeNow()
        {
            stopPreview();
            if (window)
            {
                window->requestClose();
            }
        }

        bool isPreviewPlaying() const
        {
            return previewStartedByDialog && state && state->audioDevices &&
                   state->audioDevices->isPlaying();
        }

        void togglePreview()
        {
            if (isPreviewPlaying())
            {
                stopPreview();
            }
            else
            {
                startPreview();
            }
        }

        void startPreview()
        {
            if (!state || !state->audioDevices || !definition.createPreviewSession)
            {
                return;
            }

            auto &document = state->getActiveDocumentSession().document;
            if (document.getFrameCount() <= 0 || document.getChannelCount() <= 0)
            {
                return;
            }

            uint64_t start = 0;
            uint64_t end = 0;
            if (!getPreviewRange(state, start, end) || end <= start)
            {
                return;
            }

            previewSession = definition.createPreviewSession(state, settings);
            if (!previewSession)
            {
                return;
            }

            auto processor = previewSession->getProcessor();
            if (!processor)
            {
                previewSession.reset();
                return;
            }

            cupuacu::actions::requestStop(state);

            cupuacu::audio::Play playMsg{};
            playMsg.document = &document;
            playMsg.startPos = start;
            playMsg.endPos = end;
            playMsg.loopEnabled = false;
            playMsg.selectionIsActive =
                state->getActiveDocumentSession().selection.isActive();
            playMsg.selectedChannels = getPreviewSelectedChannels(state);
            playMsg.vuMeter = cupuacu::gui::getVuMeterIfPresent(state);
            playMsg.previewProcessor = std::move(processor);
            state->audioDevices->enqueue(std::move(playMsg));
            state->playbackRangeStart = start;
            state->playbackRangeEnd = end;
            previewStartedByDialog = true;
        }

        void stopPreview()
        {
            if (!previewStartedByDialog || !state || !state->audioDevices)
            {
                return;
            }

            cupuacu::actions::requestStop(state);
            previewStartedByDialog = false;
            previewSession.reset();
        }

        void layoutComponents() const
        {
            if (!window || !window->getRootComponent())
            {
                return;
            }

            float canvasW = static_cast<float>(windowWidth);
            float canvasH = static_cast<float>(windowHeight);
            if (window->getCanvas())
            {
                SDL_GetTextureSize(window->getCanvas(), &canvasW, &canvasH);
            }

            const int canvasWi = static_cast<int>(canvasW);
            const int canvasHi = static_cast<int>(canvasH);
            const int padding = std::max(4, cupuacu::gui::scaleUi(state, 12.0f));
            const int labelFontSize = state ? state->menuFontSize : 30;
            int measuredLabelWidth = 0;
            const uint8_t effectiveLabelFontSize =
                cupuacu::gui::scaleFontPointSize(state, labelFontSize);
            for (const auto &control : controls)
            {
                const auto &spec = definition.parameters[control.specIndex];
                if (spec.kind == EffectParameterKind::Toggle ||
                    spec.kind == EffectParameterKind::Action || spec.label.empty())
                {
                    continue;
                }
                const auto [textWidth, textHeight] =
                    cupuacu::gui::measureText(spec.label, effectiveLabelFontSize);
                measuredLabelWidth = std::max(measuredLabelWidth, textWidth);
                measuredLabelWidth = std::max(measuredLabelWidth, textHeight);
            }
            const int labelWidth = std::max(
                std::max(88, cupuacu::gui::scaleUi(state, 104.0f)),
                measuredLabelWidth + std::max(8, padding / 2));
            const int inputWidth = std::max(90, cupuacu::gui::scaleUi(state, 110.0f));
            const int rowHeight = std::max(30, cupuacu::gui::scaleUi(state, 34.0f));
            const int sliderHeight = std::max(22, cupuacu::gui::scaleUi(state, 24.0f));
            const int toggleWidth = std::max(72, cupuacu::gui::scaleUi(state, 88.0f));

            window->getRootComponent()->setSize(canvasWi, canvasHi);
            background->setBounds(0, 0, canvasWi, canvasHi);

            int y = padding;
            for (const auto &control : controls)
            {
                const auto &spec = definition.parameters[control.specIndex];
                switch (spec.kind)
                {
                case EffectParameterKind::Percent:
                {
                    const int sliderX = padding + labelWidth + inputWidth + padding * 2;
                    const int sliderWidth =
                        std::max(80, canvasWi - sliderX - padding);
                    control.label->setBounds(padding, y, labelWidth, rowHeight);
                    control.input->setBounds(padding + labelWidth, y, inputWidth,
                                             rowHeight);
                    control.slider->setBounds(sliderX, y, sliderWidth,
                                              sliderHeight);
                    y += rowHeight + padding;
                    break;
                }
                case EffectParameterKind::Number:
                {
                    control.label->setBounds(padding, y, labelWidth, rowHeight);
                    control.input->setBounds(
                        padding + labelWidth + padding, y,
                        canvasWi - (padding + labelWidth + padding) - padding,
                        rowHeight);
                    y += rowHeight + padding;
                    break;
                }
                case EffectParameterKind::Enum:
                {
                    control.dropdown->setItemMargin(std::max(4, padding / 2));
                    const int dropdownHeight =
                        std::max(rowHeight, control.dropdown->getRowHeight());
                    control.label->setBounds(padding, y, labelWidth, dropdownHeight);
                    control.dropdown->setBounds(
                        padding + labelWidth + padding, y,
                        canvasWi - (padding + labelWidth + padding) - padding,
                        dropdownHeight);
                    control.dropdown->setCollapsedHeight(dropdownHeight);
                    y += dropdownHeight + padding;
                    break;
                }
                case EffectParameterKind::Toggle:
                    control.toggleButton->setBounds(padding, y, toggleWidth,
                                                    rowHeight);
                    y += rowHeight + padding;
                    break;
                case EffectParameterKind::Action:
                    control.toggleButton->setBounds(padding, y,
                                                    std::max(toggleWidth,
                                                             cupuacu::gui::scaleUi(
                                                                 state, 140.0f)),
                                                    rowHeight);
                    y += rowHeight + padding;
                    break;
                }
            }

            if (!actionButtons.empty())
            {
                y += padding;
                const int gap = padding;
                const int buttonWidth = std::max(
                    72, (canvasWi - padding * 2 -
                         gap * (static_cast<int>(actionButtons.size()) - 1)) /
                            static_cast<int>(actionButtons.size()));
                for (std::size_t i = 0; i < actionButtons.size(); ++i)
                {
                    actionButtons[i]->setBounds(
                        padding + static_cast<int>(i) * (buttonWidth + gap), y,
                        buttonWidth, rowHeight);
                }
            }

            const int bottomButtonWidth =
                std::max(96, cupuacu::gui::scaleUi(state, 120.0f));
            const int bottomY = canvasHi - padding - rowHeight;
            if (previewPanel)
            {
                previewPanel->layout(
                    SDL_Rect{padding, y + padding, canvasWi - padding * 2,
                             std::max(0, bottomY - y - padding)});
            }

            if (previewButton)
            {
                previewButton->setBounds(
                    canvasWi - padding * 3 - bottomButtonWidth * 3, bottomY,
                    bottomButtonWidth, rowHeight);
                cancelButton->setBounds(
                    canvasWi - padding * 2 - bottomButtonWidth * 2, bottomY,
                    bottomButtonWidth, rowHeight);
                applyButton->setBounds(canvasWi - padding - bottomButtonWidth,
                                       bottomY, bottomButtonWidth, rowHeight);
            }
            else
            {
                cancelButton->setBounds(
                    canvasWi - padding * 2 - bottomButtonWidth * 2, bottomY,
                    bottomButtonWidth, rowHeight);
                applyButton->setBounds(canvasWi - padding - bottomButtonWidth,
                                       bottomY, bottomButtonWidth, rowHeight);
            }
        }
    };
} // namespace cupuacu::effects
