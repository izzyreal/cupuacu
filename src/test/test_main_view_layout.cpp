#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Component.hpp"

#include <cmath>
#include <string_view>

namespace
{
    const cupuacu::gui::Component *
    findByNameRecursive(const cupuacu::gui::Component *root,
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
            if (const auto *found = findByNameRecursive(child.get(), name))
            {
                return found;
            }
        }

        return nullptr;
    }
} // namespace

TEST_CASE("Scrollbar physical height remains stable across pixel scales", "[gui]")
{
    cupuacu::State state{};
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 8000);

    const auto *scrollBar = findByNameRecursive(ui.mainView.get(), "ScrollBar");
    REQUIRE(scrollBar != nullptr);

    state.pixelScale = 1;
    ui.mainView->resized();
    const int h1 = scrollBar->getBounds().h;
    const int physical1 = h1 * state.pixelScale;

    state.pixelScale = 2;
    ui.mainView->resized();
    const int h2 = scrollBar->getBounds().h;
    const int physical2 = h2 * state.pixelScale;

    state.pixelScale = 4;
    ui.mainView->resized();
    const int h4 = scrollBar->getBounds().h;
    const int physical4 = h4 * state.pixelScale;

    REQUIRE(std::abs(physical2 - physical1) <= 2);
    REQUIRE(std::abs(physical4 - physical1) <= 2);
    REQUIRE(h4 < h2);
    REQUIRE(h2 < h1);
}
