#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/MainView.hpp"
#include "gui/MouseEvent.hpp"
#include "gui/WaveformsUnderlay.hpp"

#include <SDL3/SDL.h>

#include <memory>

namespace
{
    cupuacu::gui::MouseEvent makeMoveEvent(const int32_t x, const int32_t y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::MOVE, x, y, static_cast<float>(x), static_cast<float>(y),
            0.0f,              0.0f,
            cupuacu::gui::MouseButtonState{false, false, false}, 0, 0.0f, 0.0f};
    }
} // namespace

TEST_CASE("Empty session integration leaves underlay mouse move safe",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile.clear();

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, &state.getActiveViewState(), "test-empty", 800,
            400, SDL_WINDOW_HIDDEN);

    auto mainView = std::make_unique<cupuacu::gui::MainView>(&state);
    mainView->setWindow(state.mainDocumentSessionWindow->getWindow());
    mainView->setBounds(0, 0, 800, 300);

    REQUIRE(state.waveforms.empty());
    REQUIRE(session.document.getChannelCount() == 0);
    REQUIRE(session.document.getFrameCount() == 0);

    auto *underlay = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::WaveformsUnderlay>(mainView.get(), "WaveformsUnderlay");
    REQUIRE(underlay != nullptr);

    const bool consumed = underlay->mouseMove(makeMoveEvent(10, 10));
    REQUIRE_FALSE(consumed);

    const auto &viewState = state.getActiveViewState();
    REQUIRE_FALSE(viewState.sampleValueUnderMouseCursor.has_value());
}
