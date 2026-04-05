#include "AmplifyEnvelopeEffect.hpp"

#include "gui/ControlPointHandle.hpp"
#include "gui/Helpers.hpp"
#include "gui/UiScale.hpp"
#include "gui/VerticalRuler.hpp"
#include "gui/WaveformOverviewPlanning.hpp"
#include "gui/WaveformSamplePointPlanning.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace cupuacu::effects
{
    namespace
    {
        constexpr int kAmplifyEnvelopeWindowWidth = 760;
        constexpr int kAmplifyEnvelopeWindowHeight = 480;
        constexpr SDL_Color kPreviewBackground{24, 24, 24, 255};
        constexpr SDL_Color kPreviewWaveformColor{82, 189, 43, 255};
        constexpr SDL_Color kPreviewMidlineColor{80, 80, 80, 255};
        constexpr SDL_Color kPreviewEnvelopeColor{235, 235, 235, 255};
        constexpr SDL_Color kPreviewGuideColor{115, 115, 115, 255};
        constexpr SDL_Color kPreviewGuideEmphasisColor{170, 170, 170, 255};
        constexpr SDL_Color kEnvelopeNodeColor{210, 110, 20, 255};
        constexpr SDL_Color kEnvelopeNodeActiveColor{240, 145, 45, 255};

        class EnvelopeNodeHandle : public cupuacu::gui::ControlPointHandle
        {
        public:
            EnvelopeNodeHandle(cupuacu::State *stateToUse,
                               const std::size_t indexToUse)
                : ControlPointHandle(stateToUse,
                                     "AmplifyEnvelopeNode:" +
                                         std::to_string(indexToUse)),
                  index(indexToUse)
            {
                setFillColors(kEnvelopeNodeColor, kEnvelopeNodeActiveColor);
            }

            std::size_t getIndex() const
            {
                return index;
            }

        private:
            std::size_t index = 0;
        };

        class AmplifyEnvelopePreview : public cupuacu::gui::Component
        {
        public:
            explicit AmplifyEnvelopePreview(cupuacu::State *stateToUse)
                : Component(stateToUse, "AmplifyEnvelopePreview")
            {
            }

            void setOnSettingsChanged(
                std::function<void(const AmplifyEnvelopeSettings &, bool)>
                    callback)
            {
                onSettingsChanged = std::move(callback);
            }

            void setSettings(AmplifyEnvelopeSettings nextSettings)
            {
                sanitizeAmplifyEnvelopeSettings(nextSettings);
                settings = std::move(nextSettings);
                rebuildHandles();
                refreshHandleBounds();
                setDirty();
            }

            bool mouseDown(const cupuacu::gui::MouseEvent &event) override
            {
                if (!event.buttonState.left)
                {
                    return false;
                }

                const int nodeIndex = hitTestNode(event.mouseXf, event.mouseYf);
                if (nodeIndex >= 0)
                {
                    if (event.numClicks >= 2 && isRemovableNode(nodeIndex))
                    {
                        auto nextSettings = settings;
                        nextSettings.points.erase(nextSettings.points.begin() +
                                                  nodeIndex);
                        commitSettings(std::move(nextSettings));
                        return true;
                    }

                    draggingNodeIndex = nodeIndex;
                    dragStartMouseX = event.mouseXf;
                    dragStartMouseY = event.mouseYf;
                    dragStartNodePosition =
                        settings.points[static_cast<std::size_t>(nodeIndex)]
                            .position;
                    dragStartNodePercent =
                        settings.points[static_cast<std::size_t>(nodeIndex)]
                            .percent;
                    updateHandleActiveStates();
                    return true;
                }

                if (const auto insertion =
                        planInsertion(event.mouseXf, event.mouseYf))
                {
                    auto nextSettings = settings;
                    nextSettings.points.insert(nextSettings.points.begin() +
                                                   insertion->insertIndex,
                                               insertion->point);
                    sanitizeAmplifyEnvelopeSettings(nextSettings);
                    commitSettings(std::move(nextSettings));
                    draggingNodeIndex = insertion->insertIndex;
                    dragStartMouseX = event.mouseXf;
                    dragStartMouseY = event.mouseYf;
                    dragStartNodePosition =
                        settings.points[insertion->insertIndex].position;
                    dragStartNodePercent =
                        settings.points[insertion->insertIndex].percent;
                    updateHandleActiveStates();
                    return true;
                }

                return false;
            }

            bool mouseMove(const cupuacu::gui::MouseEvent &event) override
            {
                if (draggingNodeIndex < 0)
                {
                    return false;
                }

                auto nextSettings = settings;
                const auto nodeIndex =
                    static_cast<std::size_t>(draggingNodeIndex);
                auto &point = nextSettings.points[nodeIndex];
                point.percent =
                    snappedPercent(yToPercent(event.mouseYf), nodeIndex);
                if (!isEndpointNode(draggingNodeIndex))
                {
                    const double minPosition =
                        nextSettings.points[nodeIndex - 1].position +
                        kAmplifyEnvelopeNodeSpacing;
                    const double maxPosition =
                        nextSettings.points[nodeIndex + 1].position -
                        kAmplifyEnvelopeNodeSpacing;
                    point.position = std::clamp(xToPosition(event.mouseXf),
                                                minPosition, maxPosition);
                }

                if ((event.mod & SDL_KMOD_SHIFT) != 0)
                {
                    const float deltaX =
                        std::fabs(event.mouseXf - dragStartMouseX);
                    const float deltaY =
                        std::fabs(event.mouseYf - dragStartMouseY);
                    if (deltaY >= deltaX)
                    {
                        point.position = dragStartNodePosition;
                    }
                    else
                    {
                        point.percent = dragStartNodePercent;
                    }
                }

                commitSettings(std::move(nextSettings));
                draggingNodeIndex = static_cast<int>(nodeIndex);
                updateHandleActiveStates();
                return true;
            }

            bool mouseUp(const cupuacu::gui::MouseEvent &event) override
            {
                (void)event;
                if (draggingNodeIndex < 0)
                {
                    return false;
                }

                draggingNodeIndex = -1;
                updateHandleActiveStates();
                return true;
            }

            void resized() override
            {
                refreshHandleBounds();
            }

            void onDraw(SDL_Renderer *renderer) override
            {
                const auto bounds = getLocalBounds();
                Helpers::fillRect(renderer, bounds, kPreviewBackground);
                if (!state)
                {
                    return;
                }

                int64_t startFrame = 0;
                int64_t frameCount = 0;
                if (!getTargetRange(state, startFrame, frameCount) ||
                    frameCount <= 0)
                {
                    return;
                }

                refreshCacheIfNeeded(startFrame, frameCount,
                                     std::max(1, bounds.w));
                drawWaveform(renderer, bounds);
                drawGuides(renderer, bounds);
                drawEnvelope(renderer);
            }

        private:
            struct CacheKey
            {
                int width = 0;
                int64_t startFrame = 0;
                int64_t frameCount = 0;
                uint64_t waveformDataVersion = 0;
                uint8_t pixelScale = 1;
                std::vector<int64_t> channels;

                bool operator==(const CacheKey &other) const = default;
            };

            struct PlannedInsertion
            {
                std::size_t insertIndex = 0;
                AmplifyEnvelopePoint point{};
            };

            std::function<void(const AmplifyEnvelopeSettings &, bool)>
                onSettingsChanged;
            AmplifyEnvelopeSettings settings = defaultAmplifyEnvelopeSettings();
            std::vector<EnvelopeNodeHandle *> handles;
            int draggingNodeIndex = -1;
            float dragStartMouseX = 0.0f;
            float dragStartMouseY = 0.0f;
            double dragStartNodePosition = 0.0;
            double dragStartNodePercent = 100.0;
            mutable std::optional<CacheKey> cachedKey;
            mutable std::vector<
                std::vector<cupuacu::gui::BlockWaveformPeakColumnPlan>>
                cachedChannelColumns;

            SDL_FPoint pointToLocal(const AmplifyEnvelopePoint &point) const
            {
                const int handleSize = handlePixelSize();
                const float left = 0.0f;
                const float right =
                    static_cast<float>(std::max(0, getWidth() - 1));
                const float top = static_cast<float>(handleSize) * 0.5f;
                const float bottom =
                    static_cast<float>(std::max(0, getHeight() - handleSize)) +
                    top;
                const float x = left + static_cast<float>(point.position) *
                                           std::max(0.0f, right - left);
                const float normalizedPercent = static_cast<float>(
                    clampAmplifyEnvelopePercent(point.percent) /
                    kAmplifyEnvelopeMaxPercent);
                const float y =
                    bottom - normalizedPercent * std::max(0.0f, bottom - top);
                return {x, y};
            }

            double xToPosition(const float x) const
            {
                const float left = 0.0f;
                const float right =
                    static_cast<float>(std::max(0, getWidth() - 1));
                const float denom = std::max(1.0f, right - left);
                return std::clamp(static_cast<double>((x - left) / denom), 0.0,
                                  1.0);
            }

            double yToPercent(const float y) const
            {
                const int handleSize = handlePixelSize();
                const float top = static_cast<float>(handleSize) * 0.5f;
                const float bottom =
                    static_cast<float>(std::max(0, getHeight() - handleSize)) +
                    top;
                const float denom = std::max(1.0f, bottom - top);
                const double normalized = std::clamp(
                    static_cast<double>(bottom - y) / denom, 0.0, 1.0);
                return clampAmplifyEnvelopePercent(normalized *
                                                   kAmplifyEnvelopeMaxPercent);
            }

            int handlePixelSize() const
            {
                return std::max<int>(1,
                                     cupuacu::gui::getWaveformSamplePointSize(
                                         state ? state->pixelScale : 1,
                                         state ? state->uiScale : 1.0f));
            }

            double snappedPercent(const double proposedPercent,
                                  const std::size_t activeNodeIndex) const
            {
                const double clamped =
                    clampAmplifyEnvelopePercent(proposedPercent);
                if (!settings.snapEnabled)
                {
                    return clamped;
                }

                std::vector<double> snapTargets{0.0, 100.0, 1000.0};
                snapTargets.reserve(snapTargets.size() +
                                    settings.points.size());
                for (std::size_t index = 0; index < settings.points.size();
                     ++index)
                {
                    if (index != activeNodeIndex)
                    {
                        snapTargets.push_back(settings.points[index].percent);
                    }
                }

                const int handleSize = handlePixelSize();
                const float top = static_cast<float>(handleSize) * 0.5f;
                const float bottom =
                    static_cast<float>(std::max(0, getHeight() - handleSize)) +
                    top;
                const double snapThreshold =
                    std::max(4.0, kAmplifyEnvelopeMaxPercent *
                                      (static_cast<double>(handleSize) * 0.25 /
                                       std::max(1.0f, bottom - top)));

                double best = clamped;
                double bestDistance = snapThreshold;
                for (const double target : snapTargets)
                {
                    const double distance = std::fabs(clamped - target);
                    if (distance <= bestDistance)
                    {
                        best = target;
                        bestDistance = distance;
                    }
                }
                return best;
            }

            bool isEndpointNode(const int nodeIndex) const
            {
                return nodeIndex == 0 ||
                       nodeIndex ==
                           static_cast<int>(settings.points.size()) - 1;
            }

            bool isRemovableNode(const int nodeIndex) const
            {
                return !isEndpointNode(nodeIndex) && settings.points.size() > 2;
            }

            int hitTestNode(const float x, const float y) const
            {
                const float threshold =
                    static_cast<float>(handlePixelSize()) * 0.5f;
                const float thresholdSq = threshold * threshold;
                for (std::size_t index = 0; index < settings.points.size();
                     ++index)
                {
                    const SDL_FPoint point =
                        pointToLocal(settings.points[index]);
                    const float dx = point.x - x;
                    const float dy = point.y - y;
                    if (dx * dx + dy * dy <= thresholdSq)
                    {
                        return static_cast<int>(index);
                    }
                }
                return -1;
            }

            std::optional<PlannedInsertion> planInsertion(const float x,
                                                          const float y) const
            {
                if (settings.points.size() < 2)
                {
                    return std::nullopt;
                }

                PlannedInsertion best{};
                float bestDistanceSq = std::numeric_limits<float>::max();
                bool found = false;
                for (std::size_t index = 1; index < settings.points.size();
                     ++index)
                {
                    const SDL_FPoint start =
                        pointToLocal(settings.points[index - 1]);
                    const SDL_FPoint end = pointToLocal(settings.points[index]);
                    const float dx = end.x - start.x;
                    const float dy = end.y - start.y;
                    const float lengthSq = dx * dx + dy * dy;
                    if (!(lengthSq > 0.0f))
                    {
                        continue;
                    }

                    const float t = std::clamp(
                        ((x - start.x) * dx + (y - start.y) * dy) / lengthSq,
                        0.0f, 1.0f);
                    const float projX = start.x + dx * t;
                    const float projY = start.y + dy * t;
                    const float distX = projX - x;
                    const float distY = projY - y;
                    const float distanceSq = distX * distX + distY * distY;
                    if (distanceSq >= bestDistanceSq)
                    {
                        continue;
                    }

                    const double left = settings.points[index - 1].position;
                    const double right = settings.points[index].position;
                    if (!(right - left > kAmplifyEnvelopeNodeSpacing * 2.0))
                    {
                        continue;
                    }

                    bestDistanceSq = distanceSq;
                    best.insertIndex = index;
                    best.point.position = std::clamp(
                        left + (right - left) * static_cast<double>(t),
                        left + kAmplifyEnvelopeNodeSpacing,
                        right - kAmplifyEnvelopeNodeSpacing);
                    best.point.percent = clampAmplifyEnvelopePercent(
                        settings.points[index - 1].percent +
                        (settings.points[index].percent -
                         settings.points[index - 1].percent) *
                            static_cast<double>(t));
                    found = true;
                }

                const float threshold =
                    static_cast<float>(handlePixelSize()) * 0.75f;
                if (!found || bestDistanceSq > threshold * threshold)
                {
                    return std::nullopt;
                }
                return best;
            }

            void commitSettings(AmplifyEnvelopeSettings nextSettings)
            {
                sanitizeAmplifyEnvelopeSettings(nextSettings);
                settings = nextSettings;
                rebuildHandles();
                refreshHandleBounds();
                setDirty();
                if (onSettingsChanged)
                {
                    onSettingsChanged(settings, true);
                }
            }

            void rebuildHandles()
            {
                removeChildrenOfType<EnvelopeNodeHandle>();
                handles.clear();
                for (std::size_t index = 0; index < settings.points.size();
                     ++index)
                {
                    handles.push_back(
                        emplaceChild<EnvelopeNodeHandle>(state, index));
                }
                updateHandleActiveStates();
            }

            void refreshHandleBounds()
            {
                const int handleSize = handlePixelSize();
                for (std::size_t index = 0; index < handles.size(); ++index)
                {
                    const SDL_FPoint center =
                        pointToLocal(settings.points[index]);
                    handles[index]->setBounds(
                        static_cast<int>(std::lround(center.x)) -
                            handleSize / 2,
                        static_cast<int>(std::lround(center.y)) -
                            handleSize / 2,
                        handleSize, handleSize);
                }
            }

            void updateHandleActiveStates()
            {
                for (std::size_t index = 0; index < handles.size(); ++index)
                {
                    handles[index]->setActive(draggingNodeIndex ==
                                              static_cast<int>(index));
                }
            }

            void refreshCacheIfNeeded(const int64_t startFrame,
                                      const int64_t frameCount,
                                      const int width) const
            {
                if (!state)
                {
                    return;
                }

                const auto channels = getTargetChannels(state);
                if (channels.empty())
                {
                    cachedChannelColumns.clear();
                    cachedKey.reset();
                    return;
                }

                const auto &document =
                    state->getActiveDocumentSession().document;
                const CacheKey key{.width = width,
                                   .startFrame = startFrame,
                                   .frameCount = frameCount,
                                   .waveformDataVersion =
                                       document.getWaveformDataVersion(),
                                   .pixelScale = state->pixelScale,
                                   .channels = channels};
                if (cachedKey.has_value() && *cachedKey == key)
                {
                    return;
                }

                cachedKey = key;
                cachedChannelColumns.clear();
                const double samplesPerPixel = std::max(
                    1.0, static_cast<double>(frameCount) / std::max(1, width));
                for (std::size_t laneIndex = 0;
                     laneIndex <
                     std::min<std::size_t>(channels.size(), std::size_t{2});
                     ++laneIndex)
                {
                    cachedChannelColumns.push_back(
                        cupuacu::gui::planWaveformOverviewPeakColumns(
                            document, static_cast<int>(channels[laneIndex]),
                            startFrame, samplesPerPixel, width,
                            state->pixelScale));
                }
            }

            void drawWaveform(SDL_Renderer *renderer,
                              const SDL_Rect &bounds) const
            {
                const int laneCount = std::max(
                    1, static_cast<int>(std::min<std::size_t>(
                           cachedChannelColumns.size(), std::size_t{2})));
                const int laneGap =
                    laneCount > 1 ? std::max(1, bounds.h / 24) : 0;
                const int laneHeight = std::max(
                    1, (bounds.h - laneGap * (laneCount - 1)) / laneCount);

                for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
                {
                    const int laneY =
                        bounds.y + laneIndex * (laneHeight + laneGap);
                    const int midY = laneY + laneHeight / 2;
                    SDL_SetRenderDrawColor(renderer, kPreviewWaveformColor.r,
                                           kPreviewWaveformColor.g,
                                           kPreviewWaveformColor.b,
                                           kPreviewWaveformColor.a);
                    const auto &laneColumns =
                        cachedChannelColumns[static_cast<std::size_t>(
                            laneIndex)];
                    for (const auto &column : laneColumns)
                    {
                        const int y1 = static_cast<int>(
                            midY - column.peak.max * (laneHeight * 0.5f));
                        const int y2 = static_cast<int>(
                            midY - column.peak.min * (laneHeight * 0.5f));
                        SDL_RenderLine(
                            renderer,
                            static_cast<float>(bounds.x + column.drawXi),
                            static_cast<float>(y1),
                            static_cast<float>(bounds.x + column.drawXi),
                            static_cast<float>(y2));
                    }

                    SDL_SetRenderDrawColor(renderer, kPreviewMidlineColor.r,
                                           kPreviewMidlineColor.g,
                                           kPreviewMidlineColor.b,
                                           kPreviewMidlineColor.a);
                    SDL_RenderLine(renderer, static_cast<float>(bounds.x),
                                   static_cast<float>(midY),
                                   static_cast<float>(bounds.x + bounds.w),
                                   static_cast<float>(midY));
                }
            }

            void drawEnvelope(SDL_Renderer *renderer) const
            {
                if (settings.points.size() < 2)
                {
                    return;
                }

                SDL_SetRenderDrawColor(
                    renderer, kPreviewEnvelopeColor.r, kPreviewEnvelopeColor.g,
                    kPreviewEnvelopeColor.b, kPreviewEnvelopeColor.a);
                for (std::size_t index = 1; index < settings.points.size();
                     ++index)
                {
                    const SDL_FPoint start =
                        pointToLocal(settings.points[index - 1]);
                    const SDL_FPoint end = pointToLocal(settings.points[index]);
                    SDL_RenderLine(renderer, start.x, start.y, end.x, end.y);
                }
            }

            void drawGuides(SDL_Renderer *renderer,
                            const SDL_Rect &bounds) const
            {
                const auto drawGuide =
                    [&](const double percent, const bool emphasized)
                {
                    const float y =
                        pointToLocal(AmplifyEnvelopePoint{0.0, percent}).y;
                    const SDL_Color color = emphasized
                                                ? kPreviewGuideEmphasisColor
                                                : kPreviewGuideColor;
                    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
                                           color.a);
                    SDL_RenderLine(renderer, static_cast<float>(bounds.x), y,
                                   static_cast<float>(bounds.x + bounds.w), y);
                };

                drawGuide(0.0, false);
                drawGuide(100.0, true);
                drawGuide(1000.0, false);
            }
        };

        class AmplifyEnvelopePreviewPanel
            : public EffectPreviewPanel<AmplifyEnvelopeSettings>
        {
        public:
            void
            build(cupuacu::State *state, cupuacu::gui::Component *root,
                  std::function<void(const AmplifyEnvelopeSettings &, bool)>
                      onSettingsChanged) override
            {
                ownerState = state;
                ruler = root->emplaceChild<cupuacu::gui::VerticalRuler>(
                    state, "AmplifyEnvelopePreviewPanel");
                ruler->setMarkers({{1.0, "1000%", false},
                                   {0.5, "500%", false},
                                   {0.2, "200%", false},
                                   {0.1, "100%", true},
                                   {0.0, "0%", false}});
                preview = root->emplaceChild<AmplifyEnvelopePreview>(state);
                preview->setOnSettingsChanged(std::move(onSettingsChanged));
            }

            void sync(const AmplifyEnvelopeSettings &settings) override
            {
                if (preview)
                {
                    preview->setSettings(settings);
                }
            }

            void layout(const SDL_Rect &bounds) override
            {
                if (!preview || !ruler)
                {
                    return;
                }

                const int rulerWidth = ruler->getPreferredWidth();
                const int gap =
                    ownerState ? cupuacu::gui::scaleUi(ownerState, 10.0f) : 10;
                const int previewHeight = std::max(0, bounds.h);
                const int handleInset =
                    std::max<int>(
                        1, cupuacu::gui::getWaveformSamplePointSize(
                               ownerState ? ownerState->pixelScale : 1,
                               ownerState ? ownerState->uiScale : 1.0f)) /
                    2;
                ruler->setVerticalInsets(handleInset, handleInset);
                ruler->setBounds(bounds.x, bounds.y, rulerWidth, previewHeight);
                preview->setBounds(bounds.x + rulerWidth + gap, bounds.y,
                                   std::max(0, bounds.w - rulerWidth - gap),
                                   previewHeight);
            }

        private:
            cupuacu::State *ownerState = nullptr;
            cupuacu::gui::VerticalRuler *ruler = nullptr;
            AmplifyEnvelopePreview *preview = nullptr;
        };

        EffectDialogDefinition<AmplifyEnvelopeSettings>
        makeAmplifyEnvelopeDefinition()
        {
            EffectDialogDefinition<AmplifyEnvelopeSettings> definition{};
            definition.title = "Amplify Envelope";
            definition.loadSettings = [](cupuacu::State *state)
            {
                auto settings = state->effectSettings.amplifyEnvelope;
                sanitizeAmplifyEnvelopeSettings(settings);
                return settings;
            };
            definition.saveSettings =
                [](cupuacu::State *state,
                   const AmplifyEnvelopeSettings &settings)
            {
                auto saved = settings;
                sanitizeAmplifyEnvelopeSettings(saved);
                state->effectSettings.amplifyEnvelope = saved;
            };
            definition.applySettings =
                [](cupuacu::State *state,
                   const AmplifyEnvelopeSettings &settings)
            {
                performAmplifyEnvelope(state, settings);
            };
            definition.createPreviewSession =
                [](cupuacu::State *, const AmplifyEnvelopeSettings &settings)
            {
                return std::make_shared<AmplifyEnvelopePreviewSession>(
                    settings);
            };
            definition.createPreviewPanel = []()
                -> std::unique_ptr<EffectPreviewPanel<AmplifyEnvelopeSettings>>
            {
                return std::make_unique<AmplifyEnvelopePreviewPanel>();
            };
            definition.parameters.push_back(
                EffectParameterSpec<AmplifyEnvelopeSettings>::toggle(
                    "snap", "Snap",
                    [](const AmplifyEnvelopeSettings &settings)
                    {
                        return settings.snapEnabled;
                    },
                    [](AmplifyEnvelopeSettings &settings, const bool enabled)
                    {
                        settings.snapEnabled = enabled;
                    },
                    true));
            definition.parameters.push_back(
                EffectParameterSpec<AmplifyEnvelopeSettings>::number(
                    "fade-length", "Fade length (ms)",
                    [](cupuacu::State *,
                       const AmplifyEnvelopeSettings &settings)
                    {
                        return formatAmplifyEnvelopeFadeLengthMs(settings);
                    },
                    [](cupuacu::State *, AmplifyEnvelopeSettings &settings,
                       const std::string &text)
                    {
                        return parseAmplifyEnvelopeFadeLengthMs(settings, text);
                    },
                    "0123456789."));
            definition.actions.push_back(
                {"Reset",
                 [](AmplifyEnvelopeSettings &settings, cupuacu::State *)
                 {
                     resetAmplifyEnvelopeSettings(settings);
                 }});
            definition.actions.push_back(
                {"Normalize",
                 [](AmplifyEnvelopeSettings &settings, cupuacu::State *state)
                 {
                     normalizeAmplifyEnvelopeSettings(settings, state);
                 }});
            definition.actions.push_back(
                {"Fade in & out",
                 [](AmplifyEnvelopeSettings &settings, cupuacu::State *state)
                 {
                     configureAmplifyEnvelopeFadeInOut(settings, state);
                 }});
            return definition;
        }
    } // namespace

    AmplifyEnvelopeDialog::AmplifyEnvelopeDialog(cupuacu::State *stateToUse)
    {
        dialog = std::make_unique<EffectDialogWindow<AmplifyEnvelopeSettings>>(
            stateToUse, makeAmplifyEnvelopeDefinition(),
            kAmplifyEnvelopeWindowWidth, kAmplifyEnvelopeWindowHeight);
    }
} // namespace cupuacu::effects
