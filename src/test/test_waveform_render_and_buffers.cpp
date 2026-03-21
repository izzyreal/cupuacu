#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "audio/DirtyTrackingAudioBuffer.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/LabeledField.hpp"
#include "gui/ScrollBar.hpp"
#include "gui/WaveformSmoothRenderPlanning.hpp"

namespace
{
    cupuacu::gui::MouseEvent leftMouseDownAt(const int x, const int y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, x, y, static_cast<float>(x),
            static_cast<float>(y), 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }

    cupuacu::gui::MouseEvent leftMouseMoveAt(const int x, const int y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::MOVE, x, y, static_cast<float>(x),
            static_cast<float>(y), 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 0};
    }
} // namespace

TEST_CASE("Waveform smooth render planning derives interpolation input safely",
          "[gui]")
{
    const auto empty = cupuacu::gui::planWaveformSmoothRenderInput(
        0, 1.0, 0, 0.5, 8, [](const int64_t) { return 0.0f; });
    REQUIRE(empty.sampleX.empty());
    REQUIRE(empty.sampleY.empty());
    REQUIRE(empty.queryX.empty());

    const auto input = cupuacu::gui::planWaveformSmoothRenderInput(
        3, 1.0, 2, 0.5, 10,
        [](const int64_t sampleIndex)
        {
            return static_cast<float>(sampleIndex) / 10.0f;
        });

    REQUIRE(input.sampleX.size() == 6);
    REQUIRE(input.sampleY.size() == 6);
    REQUIRE(input.queryX.size() == 4);

    REQUIRE(input.sampleX.front() == Catch::Approx(-0.5));
    REQUIRE(input.sampleX.back() == Catch::Approx(4.5));
    REQUIRE(input.sampleY.front() == Catch::Approx(0.1f));
    REQUIRE(input.sampleY.back() == Catch::Approx(0.6f));
    REQUIRE(input.queryX.front() == Catch::Approx(0.0));
    REQUIRE(input.queryX.back() == Catch::Approx(3.0));
}

TEST_CASE("Waveform smooth spline evaluation and segment quads handle edge cases",
          "[gui]")
{
    REQUIRE(cupuacu::gui::evaluateWaveformSmoothSpline({}).empty());

    const cupuacu::gui::WaveformSmoothRenderInput line{
        {0.0, 1.0},
        {0.0, 1.0},
        {0.0, 0.5, 1.0}};
    const auto smoothed = cupuacu::gui::evaluateWaveformSmoothSpline(line);
    REQUIRE(smoothed.size() == 3);
    REQUIRE(smoothed.front() == Catch::Approx(0.0f));
    REQUIRE(smoothed.back() == Catch::Approx(1.0f));

    REQUIRE_FALSE(cupuacu::gui::planWaveformSmoothSegmentQuad(
                      1.0f, 1.0f, 2.0f, 2.0f, 3.0f)
                      .has_value());

    const auto horizontal = cupuacu::gui::planWaveformSmoothSegmentQuad(
        0.0f, 10.0f, 5.0f, 5.0f, 4.0f);
    REQUIRE(horizontal.has_value());
    REQUIRE(horizontal->vertices[0].x == Catch::Approx(0.0f));
    REQUIRE(horizontal->vertices[0].y == Catch::Approx(3.0f));
    REQUIRE(horizontal->vertices[1].y == Catch::Approx(7.0f));

    const auto vertical = cupuacu::gui::planWaveformSmoothSegmentQuad(
        8.0f, 8.0f, 1.0f, 9.0f, 2.0f);
    REQUIRE(vertical.has_value());
    REQUIRE(vertical->vertices[0].x == Catch::Approx(9.0f));
    REQUIRE(vertical->vertices[1].x == Catch::Approx(7.0f));
    REQUIRE(vertical->vertices[0].y == Catch::Approx(1.0f));
    REQUIRE(vertical->vertices[2].y == Catch::Approx(9.0f));
}

TEST_CASE("DirtyTrackingAudioBuffer preserves dirty bits across inserts and removals",
          "[audio]")
{
    cupuacu::audio::DirtyTrackingAudioBuffer buffer;
    const auto &bufferView = static_cast<const cupuacu::audio::AudioBuffer &>(buffer);
    buffer.resize(2, 4);

    REQUIRE_FALSE(bufferView.isDirty(0, 0));
    buffer.setSample(0, 1, 1.0f);
    buffer.setSample(1, 2, 0.5f, false);

    REQUIRE(bufferView.isDirty(0, 1));
    REQUIRE_FALSE(bufferView.isDirty(1, 2));

    buffer.insertFrames(4, 2);
    REQUIRE(buffer.getFrameCount() == 6);
    REQUIRE(bufferView.isDirty(0, 1));
    REQUIRE_FALSE(bufferView.isDirty(0, 4));
    REQUIRE_FALSE(bufferView.isDirty(1, 5));

    buffer.setSample(1, 3, -0.25f);
    REQUIRE(bufferView.isDirty(1, 3));

    buffer.insertFrames(1, 2);
    REQUIRE(buffer.getFrameCount() == 8);
    REQUIRE_FALSE(bufferView.isDirty(0, 1));
    REQUIRE_FALSE(bufferView.isDirty(0, 2));
    REQUIRE(bufferView.isDirty(0, 3));
    REQUIRE(bufferView.isDirty(1, 5));

    buffer.removeFrames(2, 3);
    REQUIRE(buffer.getFrameCount() == 5);
    REQUIRE(bufferView.isDirty(1, 2));
    REQUIRE_FALSE(bufferView.isDirty(0, 1));
    REQUIRE_FALSE(bufferView.isDirty(0, 4));
}

TEST_CASE("LabeledField marks itself dirty only when the displayed value changes",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::LabeledField field(&state, "Pos", SDL_Color{0, 0, 0, 255});
    field.setVisible(true);

    REQUIRE_FALSE(field.isDirty());
    field.setValue("");
    REQUIRE_FALSE(field.isDirty());

    field.setValue("42");
    REQUIRE(field.isDirty());
}

TEST_CASE("ScrollBar vertical drag updates value and non-left clicks are ignored",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    double value = 25.0;

    cupuacu::gui::ScrollBar bar(
        &state, cupuacu::gui::ScrollBar::Orientation::Vertical,
        [&]() { return value; },
        []() { return 0.0; },
        []() { return 100.0; },
        []() { return 20.0; },
        [&](const double next) { value = next; });
    bar.setVisible(true);
    bar.setBounds(0, 0, 12, 120);

    REQUIRE_FALSE(bar.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 6, 20, 6.0f, 20.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{false, true, false}, 1}));

    REQUIRE(bar.mouseDown(leftMouseDownAt(6, 105)));
    REQUIRE(value > 70.0);
    REQUIRE(value <= 100.0);

    REQUIRE(bar.mouseMove(leftMouseMoveAt(6, 200)));
    REQUIRE(value == Catch::Approx(100.0));

    REQUIRE(bar.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP, 6, 200, 6.0f, 200.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
}
