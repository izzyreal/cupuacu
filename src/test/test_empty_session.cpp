#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/MainView.hpp"
#include "gui/Component.hpp"
#include "gui/MouseEvent.hpp"
#include "gui/WaveformsUnderlay.hpp"

#include <memory>
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

    cupuacu::gui::MouseEvent makeMoveEvent(const int32_t x, const int32_t y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::MOVE, x, y, static_cast<float>(x), static_cast<float>(y),
            0.0f,              0.0f,
            cupuacu::gui::MouseButtonState{false, false, false}, 0, 0.0f, 0.0f};
    }
} // namespace

TEST_CASE("State always contains an active document session", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    REQUIRE(session.document.getChannelCount() == 0);
    REQUIRE(session.document.getFrameCount() == 0);

    session.cursor = 123;
    session.syncSelectionAndCursorToDocumentLength();
    REQUIRE(session.cursor == 0);
}

TEST_CASE("Empty session underlay mouse move is safe", "[gui][session]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.currentFile.clear();

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test-empty", 800, 400, SDL_WINDOW_HIDDEN);

    auto mainView = std::make_unique<cupuacu::gui::MainView>(&state);
    state.mainView = mainView.get();
    mainView->setBounds(0, 0, 800, 300);

    REQUIRE(state.waveforms.empty());
    REQUIRE(session.document.getChannelCount() == 0);
    REQUIRE(session.document.getFrameCount() == 0);

    auto *underlayComponent =
        findByNameRecursive(mainView.get(), "WaveformsUnderlay");
    REQUIRE(underlayComponent != nullptr);
    auto *underlay =
        dynamic_cast<cupuacu::gui::WaveformsUnderlay *>(underlayComponent);
    REQUIRE(underlay != nullptr);

    const bool consumed = underlay->mouseMove(makeMoveEvent(10, 10));
    REQUIRE_FALSE(consumed);
    const auto &viewState = state.mainDocumentSessionWindow->getViewState();
    REQUIRE_FALSE(viewState.sampleValueUnderMouseCursor.has_value());
}
