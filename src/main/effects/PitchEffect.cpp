#include "PitchEffect.hpp"
#include "PitchProcessing.hpp"
#include "EffectTargeting.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "actions/Play.hpp"
#include "audio/AudioDevices.hpp"
#include "concurrency/DeferredRelease.hpp"
#include "gui/Colors.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/OpaqueRect.hpp"
#include "gui/SecondaryWindowLifecycle.hpp"
#include "gui/Slider.hpp"
#include "gui/TextInput.hpp"
#include "gui/TextButton.hpp"
#include "gui/VuMeterAccess.hpp"
#include <atomic>
#include <iomanip>
#include <sstream>

namespace cupuacu::effects
{
    struct PitchDialog::Impl
    {
        struct RenderJob
        {
            std::atomic<bool> canceled{false}, done{false};
            std::atomic<double> progress{0};
            std::string error;
            std::shared_ptr<const storage::AudioEditRevision> result;
        };
        struct Root : gui::Component
        {
            std::function<void()> tick;
            Root(State *s, std::function<void()> f)
                : Component(s, "PitchRoot"), tick(std::move(f))
            {
            }
            void timerCallback() override
            {
                tick();
            }
        };
        State *state;
        std::unique_ptr<gui::Window> window;
        PitchSettings settings;
        uint64_t tabId = 0, version = 0;
        int64_t start = 0, frames = 0;
        std::vector<int64_t> channels;
        gui::Selection<double> selection{0};
        std::shared_ptr<const storage::AudioReader> source;
        std::shared_ptr<const storage::AudioEditRevision> sourceRevision;
        std::shared_ptr<audio::AudioBuffer> sourceBuffer;
        std::shared_ptr<RenderJob> job;
        std::shared_ptr<const storage::AudioEditRevision> rendered;
        bool requested = false, playing = false, sawPlaying = false;
        bool pendingApply = false;
        bool inputError = false, syncing = false;
        uint64_t restartAt = 0, playbackAt = 0;
        std::string displayedValue, message;
        gui::OpaqueRect *background = nullptr;
        gui::Label *factorLabel = nullptr, *typeLabel = nullptr,
                   *status = nullptr, *range = nullptr;
        gui::Slider *slider = nullptr;
        gui::TextInput *value = nullptr;
        gui::DropdownMenu *unit = nullptr, *type = nullptr;
        gui::TextButton *preview = nullptr, *apply = nullptr, *cancel = nullptr,
                        *reset = nullptr;

        explicit Impl(State *s) : state(s), settings(s->effectSettings.pitch)
        {
            if (!state->getActiveTab())
            {
                return;
            }
            tabId = state->getActiveTab()->id;
            auto &session = state->getActiveDocumentSession();
            version = session.document.getWaveformDataVersion();
            selection = session.selection;
            getTargetRange(state, start, frames);
            channels = getTargetChannels(state);
            source = concurrency::releaseOnWorker(session.getAudioReader());
            sourceRevision =
                concurrency::releaseOnWorker(session.getEditRevision());
            if (!sourceRevision)
            {
                sourceBuffer = concurrency::releaseOnWorker(
                    session.document.getAudioBuffer());
            }
            SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#if !defined(__linux__)
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
            window =
                std::make_unique<gui::Window>(state, "Pitch", 680, 300, flags);
            if (!window->isOpen())
            {
                return;
            }
            SDL_SetWindowMinimumSize(window->getSdlWindow(), 640, 300);
            gui::attachSecondaryWindow(state, window.get(), true);
            auto root = std::make_unique<Root>(state,
                                               [this]
                                               {
                                                   poll();
                                               });
            root->setVisible(true);
            background = root->emplaceChild<gui::OpaqueRect>(
                state, gui::Colors::background);
            factorLabel = root->emplaceChild<gui::Label>(state, "Factor");
            typeLabel = root->emplaceChild<gui::Label>(state, "Type");
            range = root->emplaceChild<gui::Label>(state);
            status = root->emplaceChild<gui::Label>(state);
            for (auto *label : {factorLabel, typeLabel, range, status})
            {
                label->setFontSize(state->menuFontSize - 6);
            }
            value = root->emplaceChild<gui::TextInput>(state);
            value->setFontSize(state->menuFontSize - 6);
            value->setAllowedCharacters("-+0123456789.");
            value->setConsumeEnterKey(true);
            value->setOnTextChanged(
                [this](const std::string &)
                {
                    if (!syncing)
                    {
                        readValue(false);
                    }
                });
            value->setOnEditingFinished(
                [this](const std::string &)
                {
                    readValue(true);
                });
            value->setOnEditingCanceled(
                [this]
                {
                    inputError = false;
                    sync();
                });
            slider = root->emplaceChild<gui::Slider>(
                state,
                [this]
                {
                    return 12.0 * std::log2(settings.factor);
                },
                []
                {
                    return -48.0;
                },
                []
                {
                    return 48.0;
                },
                [this](double v)
                {
                    settings.factor = std::exp2(v / 12.0);
                    inputError = false;
                    changed();
                    sync();
                });
            unit = root->emplaceChild<gui::DropdownMenu>(state);
            unit->setFontSize(state->menuFontSize - 6);
            unit->setItems({"%", "semitones"});
            unit->setSelectedIndex(int(settings.unit));
            unit->setOnSelectionChanged(
                [this](int index)
                {
                    settings.unit = static_cast<PitchUnit>(index);
                    inputError = false;
                    state->effectSettings.pitch = settings;
                    sync();
                });
            type = root->emplaceChild<gui::DropdownMenu>(state);
            type->setFontSize(state->menuFontSize - 6);
            type->setItems({"Resample", "Stretch"});
            type->setSelectedIndex(int(settings.type));
            type->setOnSelectionChanged(
                [this](int index)
                {
                    settings.type = static_cast<PitchType>(index);
                    changed();
                    sync();
                });
            reset = root->emplaceChild<gui::TextButton>(state, "Reset");
            preview = root->emplaceChild<gui::TextButton>(state, "Preview");
            cancel = root->emplaceChild<gui::TextButton>(state, "Cancel");
            apply = root->emplaceChild<gui::TextButton>(state, "Apply");
            for (auto *button : {reset, preview, cancel, apply})
            {
                button->setFontSize(state->menuFontSize - 6);
                button->setTriggerOnMouseUp(true);
            }
            reset->setOnPress(
                [this]
                {
                    settings.factor = 1.0;
                    inputError = false;
                    changed();
                    sync();
                });
            preview->setOnPress(
                [this]
                {
                    if (requested)
                    {
                        stop();
                    }
                    else if (valid() && readValue(true))
                    {
                        if (state->audioDevices)
                        {
                            actions::requestStop(state);
                        }
                        requested = true;
                        restartAt = SDL_GetTicks();
                        message.clear();
                    }
                    syncStatus();
                });
            cancel->setOnPress(
                [this]
                {
                    close();
                });
            apply->setOnPress(
                [this]
                {
                    applyNow();
                });
            window->setDefaultAction(
                [this]
                {
                    applyNow();
                });
            window->setCancelAction(
                [this]
                {
                    close();
                });
            window->setOnClose(
                [this]
                {
                    stop();
                    rendered.reset();
                    source.reset();
                    sourceRevision.reset();
                    sourceBuffer.reset();
                    gui::detachSecondaryWindow(state, window.get());
                });
            window->setOnResize(
                [this]
                {
                    layout();
                });
            window->setRootComponent(std::move(root));
            sync();
            layout();
            window->renderFrame();
        }

        ~Impl()
        {
            stop();
            gui::detachSecondaryWindow(state, window.get());
        }

        bool sameTarget() const
        {
            if (!state->getActiveTab() || state->getActiveTab()->id != tabId)
            {
                return false;
            }
            const auto &s = state->getActiveDocumentSession();
            if (s.getEditRevision() != sourceRevision ||
                (!sourceRevision &&
                 s.document.getAudioBuffer() != sourceBuffer))
            {
                return false;
            }
            if (s.document.getWaveformDataVersion() != version ||
                s.selection.isActive() != selection.isActive())
            {
                return false;
            }
            int64_t currentStart = 0, currentFrames = 0;
            return getTargetRange(state, currentStart, currentFrames) &&
                   currentStart == start && currentFrames == frames &&
                   getTargetChannels(state) == channels;
        }
        bool valid() const
        {
            return sameTarget() && !inputError && frames > 0 &&
                   !channels.empty() &&
                   (settings.type == PitchType::Stretch ||
                    channels.size() == std::size_t(source->shape().channels));
        }
        bool readValue(bool finish)
        {
            if (value->getText() == displayedValue && !inputError)
            {
                return true;
            }
            auto next = settings;
            if (!setPitchDisplayValue(next, value->getText()))
            {
                inputError = true;
                if (job)
                {
                    job->canceled = true;
                }
                job.reset();
                rendered.reset();
                stopPlayback();
                message = "Enter a factor within the displayed range.";
                syncStatus();
                return false;
            }
            inputError = false;
            if (next.factor != settings.factor)
            {
                settings = next;
                changed();
            }
            message.clear();
            if (finish)
            {
                sync();
            }
            else
            {
                syncStatus();
            }
            return true;
        }
        void stopPlayback()
        {
            if (playing)
            {
                actions::requestStop(state);
                state->renderedEffectPreview = false;
            }
            playing = false;
            sawPlaying = false;
        }
        void stop()
        {
            requested = false;
            if (job)
            {
                job->canceled = true;
            }
            job.reset();
            stopPlayback();
        }
        void changed()
        {
            pendingApply = false;
            state->effectSettings.pitch = settings;
            if (job)
            {
                job->canceled = true;
            }
            job.reset();
            rendered.reset();
            stopPlayback();
            message.clear();
            restartAt = SDL_GetTicks() + 250;
            if (!valid())
            {
                requested = false;
            }
        }
        void sync()
        {
            syncing = true;
            std::ostringstream text;
            text << std::fixed << std::setprecision(2)
                 << pitchDisplayValue(settings);
            displayedValue = text.str();
            value->setText(displayedValue);
            range->setText(settings.unit == PitchUnit::Percent
                               ? "6.25% – 1600%  ·  100% is unchanged"
                               : "−48 – +48 semitones  ·  0 is unchanged");
            slider->setDirty();
            syncing = false;
            syncStatus();
        }
        void syncStatus()
        {
            if (!status)
            {
                return;
            }
            std::string text = message;
            if (!sameTarget())
            {
                text = "The target audio changed. Reopen Pitch to continue.";
            }
            else if (settings.type == PitchType::Resample &&
                     channels.size() != std::size_t(source->shape().channels))
            {
                text = "Resample requires both channels.";
            }
            else if (job)
            {
                text = "Rendering preview… " +
                       std::to_string(int(job->progress.load() * 100)) + "%";
            }
            else if (text.empty())
            {
                text = settings.type == PitchType::Resample
                           ? "Changes pitch and duration"
                           : "Changes pitch; preserves duration";
            }
            status->setText(text);
            preview->setText(requested ? "Stop" : "Preview");
            preview->setEnabled(!pendingApply && (requested || valid()));
            apply->setEnabled(valid() && !pendingApply);
        }
        void beginRender()
        {
            static std::atomic<uint64_t> sequence{0};
            auto next = std::make_shared<RenderJob>();
            job = next;
            const auto directory =
                state->paths->statePath() /
                ("pitch-preview-" + std::to_string(SDL_GetTicksNS()) + "-" +
                 std::to_string(++sequence));
            auto reader = source;
            const auto s = settings;
            const auto selected = channels;
            const auto first = start, length = frames;
            try
            {
                state->taskScheduler->submit(
                    [next, reader, s, selected, first, length, directory]
                    {
                        try
                        {
                            next->result = renderPitch(
                                *reader, first, length, selected, s, directory,
                                [next](double p)
                                {
                                    if (next->canceled)
                                    {
                                        throw LongTaskCanceledError{};
                                    }
                                    next->progress = p;
                                });
                        }
                        catch (const std::exception &e)
                        {
                            next->error = e.what();
                        }
                        next->done.store(true, std::memory_order_release);
                    },
                    {.scratchBytes = 12 * 1024 * 1024,
                     .documentId = tabId,
                     .admissionFailed = [next](std::exception_ptr error)
                     {
                         try
                         {
                             std::rethrow_exception(error);
                         }
                         catch (const std::exception &e)
                         {
                             next->error = e.what();
                         }
                         next->done.store(true, std::memory_order_release);
                     }});
            }
            catch (const std::exception &e)
            {
                message = e.what();
                stop();
            }
        }
        void playRendered()
        {
            if (!state->audioDevices)
            {
                message = "Audio playback is unavailable.";
                stop();
                return;
            }
            actions::requestStop(state);
            const auto ready = state->audioDevices->prepareForPlayback(
                state->getActiveDocumentSession().document);
            if (!ready)
            {
                message = "Audio playback is unavailable.";
                stop();
                return;
            }
            audio::Play play{};
            play.readerSnapshot = rendered;
            play.startPos = 0;
            play.endPos = rendered->shape().frames;
            play.selectedChannels = SelectedChannels::BOTH;
            play.vuMeter = gui::getVuMeterIfPresent(state);
            if (!state->audioDevices->enqueue(std::move(play)))
            {
                message = "Unable to start preview.";
                stop();
                return;
            }
            state->renderedEffectPreview = true;
            state->playbackRangeStart = 0;
            state->playbackRangeEnd = rendered->shape().frames;
            state->playbackSourceFrames = rendered->shape().frames;
            playing = true;
            sawPlaying = false;
            playbackAt = SDL_GetTicks();
        }
        void poll()
        {
            if (!window || !window->isOpen())
            {
                return;
            }
            if (!sameTarget())
            {
                stop();
                rendered.reset();
                pendingApply = false;
            }
            if (pendingApply &&
                (!state->audioDevices || !state->audioDevices->isPlaying()))
            {
                pendingApply = false;
                if (valid() &&
                    actions::effects::queuePitch(state, settings, rendered))
                {
                    close();
                    return;
                }
                message =
                    "Unable to apply Pitch while another operation is active.";
            }
            if (job && job->done.load(std::memory_order_acquire))
            {
                if (!job->canceled && valid())
                {
                    rendered = job->result;
                    message = job->error;
                }
                job.reset();
                if (!rendered)
                {
                    requested = false;
                }
            }
            if (requested && !inputError && !job && !playing &&
                SDL_GetTicks() >= restartAt)
            {
                if (!valid())
                {
                    stop();
                }
                else if (rendered)
                {
                    playRendered();
                }
                else
                {
                    beginRender();
                }
            }
            if (playing && state->audioDevices)
            {
                if (state->audioDevices->isPlaying())
                {
                    sawPlaying = true;
                }
                else if (sawPlaying || SDL_GetTicks() - playbackAt > 1000)
                {
                    stop();
                }
            }
            syncStatus();
        }
        void close()
        {
            pendingApply = false;
            stop();
            rendered.reset();
            gui::requestSecondaryWindowClose(state, window.get());
        }
        void applyNow()
        {
            if (!readValue(true) || !valid())
            {
                return;
            }
            stop();
            // Stop is consumed by the audio callback. Wait for its
            // acknowledgement before asking the mutation gate to start the
            // effect.
            if (state->audioDevices)
            {
                actions::requestStop(state);
            }
            pendingApply = true;
            poll();
        }
        void layout()
        {
            if (!window || !window->getRootComponent())
            {
                return;
            }
            float w = 680, h = 300;
            if (window->getCanvas())
            {
                SDL_GetTextureSize(window->getCanvas(), &w, &h);
            }
            const int p = gui::scaleUi(state, 12.f),
                      row = gui::scaleUi(state, 36.f);
            const int label = gui::scaleUi(state, 80.f),
                      input = gui::scaleUi(state, 100.f);
            const int units = gui::scaleUi(state, 180.f);
            auto *root = window->getRootComponent();
            root->setSize(int(w), int(h));
            background->setBounds(0, 0, int(w), int(h));
            factorLabel->setBounds(p, p, label, row);
            value->setBounds(p + label, p, input, row);
            unit->setBounds(int(w) - units - p, p, units, row);
            slider->setBounds(
                2 * p + label + input, p,
                std::max(20, int(w) - units - label - input - 4 * p), row);
            range->setBounds(p + label, 2 * p + row, int(w) - label - 2 * p,
                             row);
            typeLabel->setBounds(p, 3 * p + 2 * row, label, row);
            type->setBounds(p + label, 3 * p + 2 * row, int(w) - label - 2 * p,
                            row);
            status->setBounds(p, 4 * p + 3 * row, int(w) - 2 * p, row);
            int x = p;
            const int bw = (int(w) - 5 * p) / 4;
            for (auto *button : {reset, preview, cancel, apply})
            {
                button->setBounds(x, int(h) - row - p, bw, row);
                x += bw + p;
            }
        }
    };

    PitchDialog::PitchDialog(State *s) : impl(std::make_unique<Impl>(s)) {}
    PitchDialog::~PitchDialog() = default;
    bool PitchDialog::isOpen() const
    {
        return impl->window && impl->window->isOpen();
    }
    void PitchDialog::raise() const
    {
        gui::raiseSecondaryWindow(impl->window.get());
    }
    gui::Window *PitchDialog::getWindow() const
    {
        return impl->window.get();
    }
} // namespace cupuacu::effects
