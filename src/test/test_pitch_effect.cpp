#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "effects/PitchProcessing.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "actions/effects/RevisionEffect.hpp"
#include "storage/DocumentAudioReader.hpp"
#include "storage/AudioSourceBuilder.hpp"
#include "TestPaths.hpp"
#include "BackgroundEffectTestUtil.hpp"
#include <cmath>
#include <numbers>

using namespace cupuacu;
using namespace cupuacu::effects;

namespace
{
    struct Tone : storage::AudioReader
    {
        storage::AudioShape dimensions{48000, 2, 48000, SampleFormat::FLOAT32};
        storage::AudioShape shape() const override
        {
            return dimensions;
        }
        void readChannel(int c, int64_t start,
                         std::span<float> output) const override
        {
            validateRange(dimensions, c, start, output.size());
            for (std::size_t i = 0; i < output.size(); ++i)
            {
                output[i] = float(0.25 * std::sin(2 * std::numbers::pi * 440 *
                                                  (start + i) / 48000.0));
            }
        }
    };
    double frequency(const storage::AudioReader &r)
    {
        const auto n = r.shape().frames;
        std::vector<float> samples(n);
        r.readChannel(0, 0, samples);
        std::vector<int64_t> crossings;
        for (int64_t i = n / 4 + 1; i < n * 3 / 4; ++i)
        {
            if (samples[i - 1] <= 0 && samples[i] > 0)
            {
                crossings.push_back(i);
            }
        }
        REQUIRE(crossings.size() > 10);
        return double(crossings.size() - 1) * r.shape().sampleRate /
               (crossings.back() - crossings.front());
    }
} // namespace

TEST_CASE("Pitch units preserve a positive factor and validate input",
          "[effects][pitch]")
{
    PitchSettings s;
    REQUIRE(pitchDisplayValue(s) == 100);
    REQUIRE(setPitchDisplayValue(s, "6.25"));
    REQUIRE(s.factor == 1.0 / 16);
    s.unit = PitchUnit::Semitones;
    REQUIRE(pitchDisplayValue(s) == -48);
    REQUIRE(setPitchDisplayValue(s, "48"));
    REQUIRE(s.factor == 16);
    REQUIRE_FALSE(setPitchDisplayValue(s, "49"));
    REQUIRE_FALSE(setPitchDisplayValue(s, "nan"));
    REQUIRE_FALSE(setPitchDisplayValue(s, "12x"));
    s.unit = PitchUnit::Percent;
    REQUIRE_FALSE(setPitchDisplayValue(s, "0"));
    REQUIRE_FALSE(setPitchDisplayValue(s, "-500"));
    REQUIRE_THROWS(pitchOutputFrames(INT64_MAX, {.factor = 0.0625}));
}

TEST_CASE("Pitch shifts octaves with the selected duration policy",
          "[effects][pitch]")
{
    Tone tone;
    for (auto type : {PitchType::Resample, PitchType::Stretch})
    {
        for (double factor : {0.5, 2.0})
        {
            CAPTURE(type, factor);
            auto result = renderPitch(tone, 0, tone.shape().frames, {0, 1},
                                      {.factor = factor, .type = type},
                                      test::makeUniqueTestRoot("pitch"));
            REQUIRE(
                result->shape().frames ==
                (type == PitchType::Stretch ? 48000 : int64_t(48000 / factor)));
            REQUIRE(frequency(*result) ==
                    Catch::Approx(440 * factor).epsilon(0.02));
            std::vector<float> left(result->shape().frames), right(left.size());
            result->readChannel(0, 0, left);
            result->readChannel(1, 0, right);
            double squaredError = 0;
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                REQUIRE(std::isfinite(left[i]));
                REQUIRE(std::isfinite(right[i]));
                squaredError += std::pow(double(left[i]) - right[i], 2);
            }
            // The phase-vocoder's channel phase reconstruction is approximate.
            REQUIRE(std::sqrt(squaredError / left.size()) < 0.001);
        }
    }
}

TEST_CASE("Pitch handles tiny sounds, extremes, and cancellation",
          "[effects][pitch]")
{
    Tone tone;
    for (auto type : {PitchType::Resample, PitchType::Stretch})
    {
        for (double factor : {minPitchFactor, maxPitchFactor})
        {
            for (int64_t n : {1, 7, 4097})
            {
                CAPTURE(type, factor, n);
                PitchSettings settings{.factor = factor, .type = type};
                auto result =
                    renderPitch(tone, 100, n, {0, 1}, settings,
                                test::makeUniqueTestRoot("pitch-short"));
                REQUIRE(result->shape().frames ==
                        pitchOutputFrames(n, settings));
            }
        }
    }
    REQUIRE_THROWS(renderPitch(tone, 0, 1000, {0}, {.factor = 2},
                               test::makeUniqueTestRoot("pitch-channel")));
    int checks = 0;
    auto directory = test::makeUniqueTestRoot("pitch-cancel");
    REQUIRE_THROWS_AS(renderPitch(tone, 0, 48000, {0, 1}, {.factor = 0.5},
                                  directory,
                                  [&](double)
                                  {
                                      if (++checks > 5)
                                      {
                                          throw LongTaskCanceledError{};
                                      }
                                  }),
                      LongTaskCanceledError);
    REQUIRE_FALSE(std::filesystem::exists(directory));
}

TEST_CASE(
    "Pitch applies to resident selections with exact undo and marker mapping",
    "[effects][pitch]")
{
    test::StateWithTestPaths state;
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;
    doc.initialize(SampleFormat::FLOAT32, 48000, 2, 800);
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < 800; ++i)
        {
            doc.setSample(c, i, float(i) / 1000, false);
        }
    }
    doc.replaceMarkers(
        {{1, 100, "before"}, {2, 300, "inside"}, {3, 700, "after"}});
    const auto originalMarkers = doc.getMarkers();
    session.selection.setValue1(200);
    session.selection.setValue2(600);
    session.cursor = 123;
    REQUIRE(actions::effects::queuePitch(&state, {.factor = 2}));
    test::drainPendingEffectWork(&state);
    REQUIRE(doc.getFrameCount() == 600);
    REQUIRE(doc.getSample(0, 199) == Catch::Approx(0.199));
    REQUIRE(doc.getSample(0, 400) == Catch::Approx(0.6));
    REQUIRE(doc.getMarkers()[1].frame == 250);
    REQUIRE(doc.getMarkers()[2].frame == 500);
    REQUIRE(session.selection.getLengthInt() == 200);
    REQUIRE(session.cursor == 200);
    state.undo();
    REQUIRE(doc.getFrameCount() == 800);
    REQUIRE(doc.getMarkers() == originalMarkers);
    REQUIRE(session.cursor == 123);
    REQUIRE(session.selection.getLengthInt() == 400);
    for (int i = 0; i < 800; ++i)
    {
        REQUIRE(doc.getSample(0, i) == float(i) / 1000);
    }
    state.redo();
    REQUIRE(doc.getFrameCount() == 600);
}

TEST_CASE("Pitch edits revisions and preserves the unselected channel",
          "[effects][pitch]")
{
    test::StateWithTestPaths state;
    Tone tone;
    auto original = renderPitch(tone, 0, 16000, {0, 1}, {},
                                test::makeUniqueTestRoot("pitch-source"));
    auto &session = state.getActiveDocumentSession();
    session.document.setExternalAudioShape(SampleFormat::FLOAT32, 48000, 2,
                                           16000);
    session.bindReadRevision(original);
    session.document.replaceMarkers(
        {{1, 1000, "a"}, {2, 9000, "b"}, {3, 15000, "c"}});
    session.selection.setValue1(4000);
    session.selection.setValue2(12000);
    session.cursor = 2000;
    state.getActiveViewState().selectedChannels = SelectedChannels::LEFT;
    state.errorReporter = [](auto &, auto &) {};
    REQUIRE_FALSE(actions::effects::queuePitch(&state, {.factor = 2}));
    REQUIRE_FALSE(state.backgroundEffectJob);
    const auto markers = session.document.getMarkers();
    REQUIRE(actions::effects::queuePitch(
        &state, {.factor = 2, .type = PitchType::Stretch}));
    test::drainPendingEffectWork(&state);
    auto changed = session.getEditRevision();
    REQUIRE(changed != original);
    REQUIRE(changed->shape().frames == 16000);
    std::vector<float> before(16000), after(16000);
    original->readChannel(1, 0, before);
    changed->readChannel(1, 0, after);
    REQUIRE(before == after);
    original->readChannel(0, 0, before);
    changed->readChannel(0, 0, after);
    REQUIRE(std::equal(before.begin(), before.begin() + 4000, after.begin()));
    REQUIRE(std::equal(before.begin() + 12000, before.end(),
                       after.begin() + 12000));
    REQUIRE(session.cursor == 2000);
    REQUIRE(session.document.getMarkers() == markers);
    state.undo();
    REQUIRE(session.getEditRevision() == original);
    state.redo();
    REQUIRE(session.getEditRevision() == changed);
    state.undo();
    state.getActiveViewState().selectedChannels = SelectedChannels::BOTH;
    REQUIRE(actions::effects::queuePitch(&state, {.factor = 0.5}));
    test::drainPendingEffectWork(&state);
    REQUIRE(session.getEditRevision()->shape().frames == 24000);
    REQUIRE(session.document.getMarkers()[1].frame == 14000);
    REQUIRE(session.document.getMarkers()[2].frame == 23000);
    REQUIRE(session.selection.getLengthInt() == 16000);
    state.undo();
    REQUIRE(session.getEditRevision() == original);
    REQUIRE(session.document.getMarkers() == markers);
    REQUIRE(session.cursor == 2000);
    REQUIRE(session.selection.getLengthInt() == 8000);
    const auto history = state.getActiveTab()->undoables.size();
    REQUIRE(actions::effects::queuePitch(&state, {}));
    REQUIRE_FALSE(state.backgroundEffectJob);
    REQUIRE(state.getActiveTab()->undoables.size() == history);
}

TEST_CASE("Pitch unity and silence are exact, and canceled jobs never commit",
          "[effects][pitch]")
{
    test::StateWithTestPaths state;
    auto &session = state.getActiveDocumentSession();
    session.document.initialize(SampleFormat::FLOAT32, 48000, 1, 48000);
    storage::DocumentAudioReader silence(session.document);
    for (auto type : {PitchType::Resample, PitchType::Stretch})
    {
        auto result =
            renderPitch(silence, 0, 48000, {0}, {.factor = 2, .type = type},
                        test::makeUniqueTestRoot("pitch-silence"));
        std::vector<float> data(result->shape().frames);
        result->readChannel(0, 0, data);
        REQUIRE(std::all_of(data.begin(), data.end(),
                            [](float x)
                            {
                                return x == 0;
                            }));
    }
    Tone tone;
    auto copy = renderPitch(tone, 123, 8000, {0, 1}, {},
                            test::makeUniqueTestRoot("pitch-unity"));
    std::vector<float> a(8000), b(8000);
    tone.readChannel(0, 123, a);
    copy->readChannel(0, 0, b);
    REQUIRE(a == b);
    actions::effects::BackgroundEffectRequest request;
    request.kind = actions::effects::BackgroundEffectKind::Pitch;
    request.startFrame = 0;
    request.frameCount = 48000;
    request.targetChannels = {0};
    request.pitchSettings = PitchSettings{.factor = 0.5};
    actions::effects::BackgroundEffectJob job(
        1, request, session.document, {}, nullptr, {},
        test::makeUniqueTestRoot("pitch-canceled-job"));
    job.cancel();
    job.start();
    REQUIRE(job.waitForCompletion(std::chrono::seconds(5)));
    REQUIRE(job.snapshot().canceled);
    REQUIRE_FALSE(job.takeResult());
    REQUIRE(session.document.getFrameCount() == 48000);
}

TEST_CASE("Stretch compensates latency and retains sounds at both boundaries",
          "[effects][pitch]")
{
    for (int impulse : {64, 12000, 23936})
    {
        CAPTURE(impulse);
        Document document;
        document.initialize(SampleFormat::FLOAT32, 48000, 1, 24000);
        document.setSample(0, impulse, 1.f, false);
        storage::DocumentAudioReader reader(document);
        auto result = renderPitch(reader, 0, 24000, {0},
                                  {.factor = 2, .type = PitchType::Stretch},
                                  test::makeUniqueTestRoot("pitch-latency"));
        std::vector<float> data(24000);
        result->readChannel(0, 0, data);
        double energy = 0, moment = 0;
        for (int i = 0; i < 24000; ++i)
        {
            const double squared = double(data[i]) * data[i];
            energy += squared;
            moment += squared * i;
        }
        REQUIRE(energy > 1e-6);
        REQUIRE(std::abs(moment / energy - impulse) < 1000);
    }
}

TEST_CASE(
    "Pitch expands a bounded resident selection and rejects stale completion",
    "[effects][pitch]")
{
    test::StateWithTestPaths state;
    auto &session = state.getActiveDocumentSession();
    auto &document = session.document;
    document.initialize(SampleFormat::FLOAT32, 48000, 1, 800);
    session.syncSelectionAndCursorToDocumentLength();
    session.selection.setValue1(0);
    session.selection.setValue2(800);
    REQUIRE(actions::effects::queuePitch(&state, {.factor = 0.5}));
    test::drainPendingEffectWork(&state);
    REQUIRE(document.getFrameCount() == 1600);
    REQUIRE(session.selection.getEndExclusiveInt() == 1600);
    state.undo();
    const auto history = state.getActiveTab()->undoables.size();
    std::string error;
    state.errorReporter = [&](const auto &, const auto &message)
    {
        error = message;
    };
    REQUIRE(actions::effects::queuePitch(&state, {.factor = 2}));
    REQUIRE(
        state.backgroundEffectJob->waitForCompletion(std::chrono::seconds(5)));
    document.setSample(0, 0, 0.7f);
    actions::effects::processPendingEffectWork(&state);
    REQUIRE(error.find("changed") != std::string::npos);
    REQUIRE(document.getFrameCount() == 800);
    REQUIRE(document.getSample(0, 0) == 0.7f);
    REQUIRE(state.getActiveTab()->undoables.size() == history);
}
