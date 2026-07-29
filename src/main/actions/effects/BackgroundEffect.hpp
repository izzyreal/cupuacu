#pragma once

#include "../../Document.hpp"
#include "../../State.hpp"
#include "../../effects/EffectSettings.hpp"
#include "../../effects/RemoveSilenceEffect.hpp"
#include "../../undo/UndoStore.hpp"

#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace cupuacu::actions::effects
{
    enum class BackgroundEffectKind
    {
        Reverse,
        AmplifyFade,
        Dynamics,
        AmplifyEnvelope,
        RemoveSilence,
    };

    struct BackgroundEffectRequest
    {
        BackgroundEffectKind kind = BackgroundEffectKind::Reverse;
        int targetTabIndex = -1;
        uint64_t targetTabId = 0;
        std::string description;
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        int64_t originalCursor = 0;
        bool hadSelection = false;
        std::optional<::cupuacu::effects::AmplifyFadeSettings>
            amplifyFadeSettings;
        std::optional<::cupuacu::effects::DynamicsSettings> dynamicsSettings;
        std::optional<::cupuacu::effects::AmplifyEnvelopeSettings>
            amplifyEnvelopeSettings;
        std::optional<::cupuacu::effects::RemoveSilenceSettings>
            removeSilenceSettings;
    };

    struct BackgroundEffectResult
    {
        BackgroundEffectKind kind = BackgroundEffectKind::Reverse;
        int targetTabIndex = -1;
        uint64_t targetTabId = 0;
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        std::vector<std::vector<float>> oldSamples;
        std::vector<std::vector<float>> newSamples;
        std::vector<::cupuacu::effects::SilenceRange> silenceRuns;
        std::vector<std::vector<std::vector<float>>> removedSamples;
        int64_t originalRelevantLength = 0;
        int64_t originalCursor = 0;
        bool hadSelection = false;
        bool removeSilenceRemovesDuration = false;
        std::optional<cupuacu::Document> preparedDocument;
        undo::UndoStore::SampleMatrixHandle oldSamplesHandle;
        undo::UndoStore::SampleMatrixHandle newSamplesHandle;
        undo::UndoStore::SampleCubeHandle removedSamplesHandle;
    };

    class BackgroundEffectJob
    {
    public:
        struct Snapshot
        {
            bool completed = false;
            bool success = false;
            bool canceled = false;
            BackgroundEffectRequest request;
            std::string detail;
            std::optional<double> progress;
            std::string error;
        };

        BackgroundEffectJob(std::uint64_t idToUse,
                            BackgroundEffectRequest requestToRun,
                            const cupuacu::Document &documentToRead,
                            undo::UndoStore undoStoreToUse = {});
        ~BackgroundEffectJob();

        BackgroundEffectJob(const BackgroundEffectJob &) = delete;
        BackgroundEffectJob &operator=(const BackgroundEffectJob &) = delete;

        void start();
        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] bool waitForCompletion(
            std::chrono::milliseconds timeout) const;
        [[nodiscard]] std::unique_ptr<BackgroundEffectResult> takeResult();
        [[nodiscard]] std::uint64_t getId() const;
        void cancel();

    private:
        std::uint64_t id = 0;
        BackgroundEffectRequest request;
        cupuacu::Document document;
        undo::UndoStore undoStore;
        mutable std::mutex mutex;
        mutable std::condition_variable completionCv;
        bool completed = false;
        bool success = false;
        std::string detail;
        std::optional<double> progress;
        std::string error;
        std::unique_ptr<BackgroundEffectResult> result;
        std::thread worker;
        std::atomic<bool> cancelRequested{false};

        void run();
        void publishProgress(const std::string &detailToUse,
                             std::optional<double> progressToUse);
    };

    bool queueReverse(cupuacu::State *state);
    bool queueAmplifyFade(cupuacu::State *state,
                          const ::cupuacu::effects::AmplifyFadeSettings &settings);
    bool queueDynamics(cupuacu::State *state,
                       const ::cupuacu::effects::DynamicsSettings &settings);
    bool queueAmplifyEnvelope(
        cupuacu::State *state,
        ::cupuacu::effects::AmplifyEnvelopeSettings settings);
    bool queueRemoveSilence(
        cupuacu::State *state,
        const ::cupuacu::effects::RemoveSilenceSettings &settings);
    void processPendingEffectWork(cupuacu::State *state);
} // namespace cupuacu::actions::effects
