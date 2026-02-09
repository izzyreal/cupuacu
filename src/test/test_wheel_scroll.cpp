#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Component.hpp"
#include "gui/MouseEvent.hpp"
#include "gui/WaveformsUnderlay.hpp"

#include <cmath>
#include <string_view>

namespace
{
    cupuacu::gui::Component *findByNameRecursive(cupuacu::gui::Component *root,
                                                 const std::string_view name)
    {
        if (root == nullptr)
        {
            return nullptr;
        }

        if (root->getComponentName() == name)
        {
            return root;
        }

        for (const auto &child : root->getChildren())
        {
            if (auto *found = findByNameRecursive(child.get(), name))
            {
                return found;
            }
        }

        return nullptr;
    }

    cupuacu::gui::MouseEvent makeWheelEvent(const float wheelX)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::WHEEL, 10, 10, 10.0f, 10.0f, 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{false, false, false}, 0, wheelX, 0.0f};
    }
} // namespace

TEST_CASE("Horizontal wheel scroll small gesture keeps immediate jump bounded",
          "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 200000);
    REQUIRE(ui.mainView != nullptr);

    auto *underlayComponent =
        findByNameRecursive(ui.mainView.get(), "WaveformsUnderlay");
    REQUIRE(underlayComponent != nullptr);
    auto *underlay =
        dynamic_cast<cupuacu::gui::WaveformsUnderlay *>(underlayComponent);
    REQUIRE(underlay != nullptr);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 1.0;
    updateSampleOffset(&state, 1000);

    const int64_t before = viewState.sampleOffset;
    REQUIRE(underlay->mouseWheel(makeWheelEvent(1.0f)));

    const int64_t immediateDelta = viewState.sampleOffset - before;
    REQUIRE(immediateDelta >= 4);
    REQUIRE(immediateDelta <= 5);

    for (int i = 0; i < 24; ++i)
    {
        underlay->timerCallback();
    }

    const int64_t totalDelta = viewState.sampleOffset - before;
    REQUIRE(totalDelta >= 14);
    REQUIRE(totalDelta <= 15);
}

TEST_CASE("Horizontal wheel scroll responds faster to higher gesture velocity",
          "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 200000);
    REQUIRE(ui.mainView != nullptr);

    auto *underlayComponent =
        findByNameRecursive(ui.mainView.get(), "WaveformsUnderlay");
    REQUIRE(underlayComponent != nullptr);
    auto *underlay =
        dynamic_cast<cupuacu::gui::WaveformsUnderlay *>(underlayComponent);
    REQUIRE(underlay != nullptr);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 1.0;

    updateSampleOffset(&state, 1000);
    const int64_t beforeSingle = viewState.sampleOffset;
    REQUIRE(underlay->mouseWheel(makeWheelEvent(1.0f)));
    const int64_t singleDelta = viewState.sampleOffset - beforeSingle;
    REQUIRE(singleDelta > 0);

    updateSampleOffset(&state, 1000);
    const int64_t beforeDouble = viewState.sampleOffset;
    REQUIRE(underlay->mouseWheel(makeWheelEvent(1.0f)));
    REQUIRE(underlay->mouseWheel(makeWheelEvent(1.0f)));
    const int64_t doubleDelta = viewState.sampleOffset - beforeDouble;

    REQUIRE(doubleDelta > singleDelta);
}

TEST_CASE("Horizontal wheel scroll scales proportionally with velocity",
          "[gui]")
{
    const auto measureImmediateDelta = [](const float wheelX) -> int64_t
    {
        cupuacu::State state{};
        auto ui = cupuacu::test::createSessionUi(&state, 200000);
        REQUIRE(ui.mainView != nullptr);

        auto *underlayComponent =
            findByNameRecursive(ui.mainView.get(), "WaveformsUnderlay");
        REQUIRE(underlayComponent != nullptr);
        auto *underlay =
            dynamic_cast<cupuacu::gui::WaveformsUnderlay *>(underlayComponent);
        REQUIRE(underlay != nullptr);

        auto &viewState = state.mainDocumentSessionWindow->getViewState();
        viewState.samplesPerPixel = 1.0;

        updateSampleOffset(&state, 1000);
        const int64_t before = viewState.sampleOffset;
        REQUIRE(underlay->mouseWheel(makeWheelEvent(wheelX)));
        return viewState.sampleOffset - before;
    };

    const int64_t slowDelta = measureImmediateDelta(1.0f);
    const int64_t mediumDelta = measureImmediateDelta(1.8f);
    const int64_t fastDelta = measureImmediateDelta(4.0f);

    REQUIRE(slowDelta >= 4);
    REQUIRE(slowDelta <= 5);
    REQUIRE(mediumDelta >= 8);
    REQUIRE(mediumDelta <= 9);
    REQUIRE(fastDelta >= 18);
    REQUIRE(fastDelta <= 18);
    REQUIRE(mediumDelta > slowDelta);
    REQUIRE(fastDelta > mediumDelta);
    REQUIRE(std::abs(mediumDelta - slowDelta * 1.8) <= 2.0);
    REQUIRE(std::abs(fastDelta - slowDelta * 4.0) <= 4.0);
}
