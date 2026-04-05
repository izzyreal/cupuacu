#include "RemoveSilenceEffect.hpp"

#include "gui/Helpers.hpp"
#include "gui/UiScale.hpp"
#include "gui/WaveformOverviewPlanning.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

using namespace cupuacu::effects;

namespace
{
    constexpr int kFallbackWindowWidth = 640;
    constexpr int kFallbackWindowHeight = 360;
    constexpr SDL_Color kPreviewOverlayColor{210, 110, 20, 140};
    constexpr SDL_Color kPreviewWaveformColor{82, 189, 43, 255};
    constexpr SDL_Color kPreviewMidlineColor{80, 80, 80, 255};

    std::pair<int, int> removeSilenceWindowSize(cupuacu::State *state)
    {
        int windowWidth = kFallbackWindowWidth;
        int windowHeight = kFallbackWindowHeight;
        if (auto *mainWindow =
                state && state->mainDocumentSessionWindow
                    ? state->mainDocumentSessionWindow->getWindow()
                    : nullptr;
            mainWindow && mainWindow->getSdlWindow())
        {
            int mainW = 0;
            int mainH = 0;
            if (SDL_GetWindowSize(mainWindow->getSdlWindow(), &mainW, &mainH))
            {
                windowWidth =
                    std::max(560, static_cast<int>(std::lround(mainW * 0.7)));
                windowHeight =
                    std::max(420, static_cast<int>(std::lround(mainH * 0.7)));
            }
        }
        return {windowWidth, windowHeight};
    }

    std::string formatThresholdText(cupuacu::State *state,
                                    const RemoveSilenceSettings &settings)
    {
        std::ostringstream stream;
        stream << std::fixed;
        if (!state)
        {
            stream << "0";
            return stream.str();
        }

        const auto format =
            state->getActiveDocumentSession().document.getSampleFormat();
        if (removeSilenceThresholdUnitFromIndex(settings.thresholdUnitIndex) ==
            RemoveSilenceThresholdUnit::Db)
        {
            stream << std::setprecision(1) << settings.thresholdDb;
        }
        else if (sampleValueDisplayMax(format) > 1.0)
        {
            stream << std::setprecision(0) << settings.thresholdSampleValue;
        }
        else
        {
            stream << std::setprecision(5) << settings.thresholdSampleValue;
        }
        return stream.str();
    }

    bool parseThresholdText(cupuacu::State *state,
                            RemoveSilenceSettings &settings,
                            const std::string &text)
    {
        if (!state)
        {
            return false;
        }

        try
        {
            const double value = std::stod(text);
            const auto format =
                state->getActiveDocumentSession().document.getSampleFormat();
            if (removeSilenceThresholdUnitFromIndex(
                    settings.thresholdUnitIndex) ==
                RemoveSilenceThresholdUnit::Db)
            {
                settings.thresholdDb = std::clamp(value, -120.0, 0.0);
            }
            else
            {
                settings.thresholdSampleValue = std::clamp(
                    std::fabs(value), 0.0, sampleValueDisplayMax(format));
            }
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    void applyAutoThreshold(RemoveSilenceSettings &settings,
                            cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!getTargetRange(state, startFrame, frameCount))
        {
            return;
        }

        auto &document = state->getActiveDocumentSession().document;
        const auto channels = getTargetChannels(state);
        const double absolute = computeAutoSilenceThresholdAbsolute(
            document, channels, startFrame, frameCount);
        settings.thresholdDb = thresholdDbFromAbsolute(absolute);
        settings.thresholdSampleValue = thresholdSampleValueFromAbsolute(
            document.getSampleFormat(), absolute);
    }

    class RemoveSilencePreview : public cupuacu::gui::Component
    {
    public:
        explicit RemoveSilencePreview(cupuacu::State *stateToUse)
            : Component(stateToUse, "RemoveSilenceMiniWaveform")
        {
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const auto bounds = getLocalBounds();
            Helpers::fillRect(renderer, bounds, {24, 24, 24, 255});
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

            const int width = std::max(1, bounds.w);
            const int height = std::max(1, bounds.h);
            refreshCacheIfNeeded(startFrame, frameCount, width);

            const int laneCount =
                std::max(1, static_cast<int>(std::min<std::size_t>(
                                cachedChannelColumns.size(), std::size_t{2})));
            const int laneGap = laneCount > 1 ? std::max(1, height / 24) : 0;
            const int laneHeight =
                std::max(1, (height - laneGap * (laneCount - 1)) / laneCount);

            for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
            {
                const int laneY = bounds.y + laneIndex * (laneHeight + laneGap);
                const int midY = laneY + laneHeight / 2;

                for (const auto &run : cachedRuns)
                {
                    if (const auto rect = cupuacu::gui::planFrameSpanRect(
                            run.startFrame, run.frameCount, startFrame,
                            cachedSamplesPerPixel, width, laneHeight))
                    {
                        SDL_Rect translated = *rect;
                        translated.x += bounds.x;
                        translated.y += laneY;
                        Helpers::fillRect(renderer, translated,
                                          kPreviewOverlayColor);
                    }
                }

                SDL_SetRenderDrawColor(
                    renderer, kPreviewWaveformColor.r, kPreviewWaveformColor.g,
                    kPreviewWaveformColor.b, kPreviewWaveformColor.a);
                const auto &laneColumns =
                    cachedChannelColumns[static_cast<std::size_t>(laneIndex)];
                for (const auto &column : laneColumns)
                {
                    const int y1 = static_cast<int>(
                        midY - column.peak.max * (laneHeight * 0.5f));
                    const int y2 = static_cast<int>(
                        midY - column.peak.min * (laneHeight * 0.5f));
                    SDL_RenderLine(renderer,
                                   static_cast<float>(bounds.x + column.drawXi),
                                   static_cast<float>(y1),
                                   static_cast<float>(bounds.x + column.drawXi),
                                   static_cast<float>(y2));
                }

                SDL_SetRenderDrawColor(
                    renderer, kPreviewMidlineColor.r, kPreviewMidlineColor.g,
                    kPreviewMidlineColor.b, kPreviewMidlineColor.a);
                SDL_RenderLine(renderer, static_cast<float>(bounds.x),
                               static_cast<float>(midY),
                               static_cast<float>(bounds.x + bounds.w),
                               static_cast<float>(midY));
            }
        }

    private:
        struct CacheKey
        {
            int width = 0;
            int64_t startFrame = 0;
            int64_t frameCount = 0;
            uint64_t waveformDataVersion = 0;
            uint8_t pixelScale = 1;
            int modeIndex = 0;
            int thresholdUnitIndex = 0;
            double thresholdDb = 0.0;
            double thresholdSampleValue = 0.0;
            double minimumSilenceLengthMs = 0.0;
            std::vector<int64_t> channels;

            bool operator==(const CacheKey &other) const = default;
        };

        mutable std::optional<CacheKey> cachedKey;
        mutable std::vector<SilenceRange> cachedRuns;
        mutable std::vector<
            std::vector<cupuacu::gui::BlockWaveformPeakColumnPlan>>
            cachedChannelColumns;
        mutable double cachedSamplesPerPixel = 1.0;

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
                cachedRuns.clear();
                cachedChannelColumns.clear();
                cachedKey.reset();
                return;
            }

            const auto &document = state->getActiveDocumentSession().document;
            const CacheKey key{
                .width = width,
                .startFrame = startFrame,
                .frameCount = frameCount,
                .waveformDataVersion = document.getWaveformDataVersion(),
                .pixelScale = state->pixelScale,
                .modeIndex = state->effectSettings.removeSilence.modeIndex,
                .thresholdUnitIndex =
                    state->effectSettings.removeSilence.thresholdUnitIndex,
                .thresholdDb = state->effectSettings.removeSilence.thresholdDb,
                .thresholdSampleValue =
                    state->effectSettings.removeSilence.thresholdSampleValue,
                .minimumSilenceLengthMs =
                    state->effectSettings.removeSilence.minimumSilenceLengthMs,
                .channels = channels};

            if (cachedKey.has_value() && *cachedKey == key)
            {
                return;
            }

            cachedKey = key;
            cachedSamplesPerPixel = std::max(
                1.0, static_cast<double>(frameCount) / std::max(1, width));
            cachedRuns = planSilenceRemoval(
                document, channels, startFrame, frameCount,
                currentThresholdAbsolute(state,
                                         state->effectSettings.removeSilence),
                removeSilenceModeFromIndex(
                    state->effectSettings.removeSilence.modeIndex),
                state->effectSettings.removeSilence);

            cachedChannelColumns.clear();
            for (std::size_t laneIndex = 0;
                 laneIndex <
                 std::min<std::size_t>(channels.size(), std::size_t{2});
                 ++laneIndex)
            {
                cachedChannelColumns.push_back(
                    cupuacu::gui::planWaveformOverviewPeakColumns(
                        document, static_cast<int>(channels[laneIndex]),
                        startFrame, cachedSamplesPerPixel, width,
                        state->pixelScale));
            }
        }
    };

    class RemoveSilencePreviewPanel
        : public EffectPreviewPanel<RemoveSilenceSettings>
    {
    public:
        void
        build(cupuacu::State *state, cupuacu::gui::Component *root,
              std::function<void(const RemoveSilenceSettings &, bool)>) override
        {
            ownerState = state;
            preview = root->emplaceChild<RemoveSilencePreview>(state);
        }

        void sync(const RemoveSilenceSettings &) override
        {
            if (preview)
            {
                preview->setDirty();
            }
        }

        void layout(const SDL_Rect &bounds) override
        {
            if (!preview)
            {
                return;
            }
            const int previewHeight = std::max(
                ownerState ? cupuacu::gui::scaleUi(ownerState, 180.0f) : 180,
                bounds.h);
            preview->setBounds(bounds.x, bounds.y, bounds.w, previewHeight);
        }

    private:
        cupuacu::State *ownerState = nullptr;
        RemoveSilencePreview *preview = nullptr;
    };

    EffectDialogDefinition<RemoveSilenceSettings> makeRemoveSilenceDefinition()
    {
        EffectDialogDefinition<RemoveSilenceSettings> definition{};
        definition.title = "Remove silence";
        definition.loadSettings = [](cupuacu::State *state)
        {
            auto settings = state->effectSettings.removeSilence;
            settings.minimumSilenceLengthMs =
                normalizeRemoveSilenceMinimumLengthMs(
                    settings.minimumSilenceLengthMs);
            return settings;
        };
        definition.saveSettings =
            [](cupuacu::State *state, const RemoveSilenceSettings &settings)
        {
            auto saved = settings;
            saved.minimumSilenceLengthMs =
                normalizeRemoveSilenceMinimumLengthMs(
                    saved.minimumSilenceLengthMs);
            state->effectSettings.removeSilence = saved;
        };
        definition.applySettings =
            [](cupuacu::State *state, const RemoveSilenceSettings &settings)
        {
            performRemoveSilence(state, settings);
        };
        definition.createPreviewPanel =
            []() -> std::unique_ptr<EffectPreviewPanel<RemoveSilenceSettings>>
        {
            return std::make_unique<RemoveSilencePreviewPanel>();
        };

        definition.parameters.push_back(
            EffectParameterSpec<RemoveSilenceSettings>::enumeration(
                "mode", "Mode", removeSilenceModeLabels(),
                [](const RemoveSilenceSettings &settings)
                {
                    return settings.modeIndex;
                },
                [](RemoveSilenceSettings &settings, const int index)
                {
                    settings.modeIndex = std::clamp(index, 0, 1);
                }));
        definition.parameters.push_back(
            EffectParameterSpec<RemoveSilenceSettings>::number(
                "threshold", "Threshold",
                [](cupuacu::State *state, const RemoveSilenceSettings &settings)
                {
                    return formatThresholdText(state, settings);
                },
                [](cupuacu::State *state, RemoveSilenceSettings &settings,
                   const std::string &text)
                {
                    return parseThresholdText(state, settings, text);
                },
                "-0123456789."));
        definition.parameters.push_back(
            EffectParameterSpec<RemoveSilenceSettings>::enumeration(
                "threshold-unit", "Threshold Unit",
                removeSilenceThresholdUnitLabels(),
                [](const RemoveSilenceSettings &settings)
                {
                    return settings.thresholdUnitIndex;
                },
                [](RemoveSilenceSettings &settings, const int index)
                {
                    settings.thresholdUnitIndex = std::clamp(index, 0, 1);
                }));
        definition.parameters.push_back(
            EffectParameterSpec<RemoveSilenceSettings>::action(
                "auto-threshold", "Auto threshold",
                [](RemoveSilenceSettings &settings, cupuacu::State *state)
                {
                    applyAutoThreshold(settings, state);
                }));
        definition.parameters.push_back(
            EffectParameterSpec<RemoveSilenceSettings>::enumeration(
                "minimum-silence", "Minimum silence",
                removeSilenceMinimumLengthLabels(),
                [](const RemoveSilenceSettings &settings)
                {
                    return removeSilenceMinimumLengthIndex(
                        settings.minimumSilenceLengthMs);
                },
                [](RemoveSilenceSettings &settings, const int index)
                {
                    const auto &options = removeSilenceMinimumLengthOptionsMs();
                    if (index >= 0 && index < static_cast<int>(options.size()))
                    {
                        settings.minimumSilenceLengthMs =
                            options[static_cast<std::size_t>(index)];
                    }
                }));

        return definition;
    }
} // namespace

RemoveSilenceDialog::RemoveSilenceDialog(cupuacu::State *stateToUse)
{
    const auto [windowWidth, windowHeight] =
        removeSilenceWindowSize(stateToUse);
    dialog = std::make_unique<EffectDialogWindow<RemoveSilenceSettings>>(
        stateToUse, makeRemoveSilenceDefinition(), windowWidth, windowHeight);
}

bool RemoveSilenceDialog::isOpen() const
{
    return dialog && dialog->isOpen();
}

void RemoveSilenceDialog::raise() const
{
    if (dialog)
    {
        dialog->raise();
    }
}

cupuacu::gui::Window *RemoveSilenceDialog::getWindow() const
{
    return dialog ? dialog->getWindow() : nullptr;
}
