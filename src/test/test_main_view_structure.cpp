#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/MainView.hpp"
#include "gui/MenuBar.hpp"
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

TEST_CASE("MainView contains scroll bar waveforms timeline and four borders",
          "[gui]")
{
    cupuacu::State state{};
    auto sessionUi = cupuacu::test::createSessionUi(&state, 128);
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
        }
        else if (dynamic_cast<cupuacu::gui::Timeline *>(child) != nullptr)
        {
            ++timelineCount;
        }
        else
        {
            ++borderCount;
            for (const auto &grandchild : child->getChildren())
            {
                if (dynamic_cast<cupuacu::gui::ScrollBar *>(grandchild.get()) !=
                    nullptr)
                {
                    ++scrollBarCount;
                }
            }
        }
    }

    REQUIRE(borderCount == 4);
    REQUIRE(waveformsCount == 1);
    REQUIRE(timelineCount == 1);
    REQUIRE(scrollBarCount == 1);
}

TEST_CASE("MainView resized keeps waveform area between borders and timeline",
          "[gui]")
{
    cupuacu::State state{};
    state.pixelScale = 2;
    auto sessionUi = cupuacu::test::createSessionUi(&state, 256, false, 2, 44100,
                                                    800, 400, 300);
    auto *mainView = sessionUi.mainView.get();

    cupuacu::gui::Waveforms *waveforms = nullptr;
    cupuacu::gui::Timeline *timeline = nullptr;
    cupuacu::gui::ScrollBar *scrollBar = nullptr;
    std::vector<cupuacu::gui::Component *> borders;

    for (const auto &child : mainView->getChildren())
    {
        if (auto *candidate =
                dynamic_cast<cupuacu::gui::Waveforms *>(child.get()))
        {
            waveforms = candidate;
        }
        else if (auto *candidate =
                     dynamic_cast<cupuacu::gui::Timeline *>(child.get()))
        {
            timeline = candidate;
        }
        else
        {
            borders.push_back(child.get());
            for (const auto &grandchild : child->getChildren())
            {
                if (auto *candidate =
                        dynamic_cast<cupuacu::gui::ScrollBar *>(grandchild.get()))
                {
                    scrollBar = candidate;
                }
            }
        }
    }

    REQUIRE(waveforms != nullptr);
    REQUIRE(timeline != nullptr);
    REQUIRE(scrollBar != nullptr);
    REQUIRE(borders.size() == 4);

    mainView->setBounds(0, 0, 640, 320);

    const auto waveformsBounds = waveforms->getBounds();
    const auto timelineBounds = timeline->getBounds();
    const auto scrollBarBounds = scrollBar->getBounds();

    REQUIRE(scrollBarBounds.w == waveformsBounds.w);
    REQUIRE(scrollBarBounds.x == waveformsBounds.x);
    REQUIRE(waveformsBounds.y == scrollBarBounds.y + scrollBarBounds.h + 8);
    REQUIRE(timelineBounds.y >= waveformsBounds.y + waveformsBounds.h);
    REQUIRE(waveformsBounds.h > 0);
}

TEST_CASE("Waveforms resize evenly tiles channel components", "[gui]")
{
    cupuacu::State state{};
    auto sessionUi =
        cupuacu::test::createSessionUi(&state, 512, false, 3, 44100, 800, 400, 300);
    auto *mainView = sessionUi.mainView.get();

    cupuacu::gui::Waveforms *waveforms = nullptr;
    for (const auto &child : mainView->getChildren())
    {
        waveforms = dynamic_cast<cupuacu::gui::Waveforms *>(child.get());
        if (waveforms)
        {
            break;
        }
    }
    REQUIRE(waveforms != nullptr);

    waveforms->setBounds(0, 0, 301, 101);

    int totalHeight = 0;
    int waveformChildren = 0;
    for (const auto &child : waveforms->getChildren())
    {
        if (child->getComponentName() == "WaveformsUnderlay")
        {
            continue;
        }
        totalHeight += child->getHeight();
        ++waveformChildren;
    }

    REQUIRE(waveformChildren == 3);
    REQUIRE(totalHeight == 101);
}
