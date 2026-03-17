#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/MainView.hpp"
#include "gui/MenuBar.hpp"
#include "gui/OpaqueRect.hpp"
#include "gui/ScrollBar.hpp"
#include "gui/Timeline.hpp"
#include "gui/Waveforms.hpp"

#include <vector>

namespace
{
    std::vector<cupuacu::gui::Component *> componentChildren(
        cupuacu::gui::Component *parent)
    {
        std::vector<cupuacu::gui::Component *> result;
        for (const auto &child : parent->getChildren())
        {
            result.push_back(child.get());
        }
        return result;
    }
} // namespace

TEST_CASE("MainView integration contains expected coarse structure",
          "[integration]")
{
    cupuacu::State state{};
    auto sessionUi = cupuacu::test::integration::createSessionUi(&state, 128);
    auto *mainView = sessionUi.mainView.get();

    auto children = componentChildren(mainView);
    int borderCount = 0;
    int waveformsCount = 0;
    int timelineCount = 0;
    int scrollBarCount = 0;

    for (auto *child : children)
    {
        if (dynamic_cast<cupuacu::gui::Waveforms *>(child) != nullptr)
        {
            ++waveformsCount;
            continue;
        }
        if (dynamic_cast<cupuacu::gui::Timeline *>(child) != nullptr)
        {
            ++timelineCount;
            continue;
        }

        auto *border = dynamic_cast<cupuacu::gui::OpaqueRect *>(child);
        REQUIRE(border != nullptr);
        ++borderCount;

        for (const auto &grandchild : border->getChildren())
        {
            if (dynamic_cast<cupuacu::gui::ScrollBar *>(grandchild.get()) != nullptr)
            {
                ++scrollBarCount;
            }
        }
    }

    REQUIRE(borderCount == 4);
    REQUIRE(waveformsCount == 1);
    REQUIRE(timelineCount == 1);
    REQUIRE(scrollBarCount == 1);
}
