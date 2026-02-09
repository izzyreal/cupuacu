#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Component.hpp"
#include "gui/TransportButtonsContainer.hpp"
#include "gui/Waveform.hpp"

#include <cmath>
#include <string_view>
#include <tuple>

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

TEST_CASE("Waveform channels fully tile waveforms height without gaps", "[gui]")
{
    cupuacu::State state{};
    [[maybe_unused]] auto ui =
        cupuacu::test::createSessionUi(&state, 8000, false, 2, 44100, 801, 401, 303);

    REQUIRE(state.mainView != nullptr);
    REQUIRE(state.waveforms.size() == 2);

    state.pixelScale = 4;
    ui.mainView->resized();

    const auto *waveformsContainer =
        findByNameRecursive(ui.mainView.get(), "Waveforms");
    REQUIRE(waveformsContainer != nullptr);
    const auto waveformsBounds = waveformsContainer->getBounds();
    const SDL_Rect ch0 = state.waveforms[0]->getBounds();
    const SDL_Rect ch1 = state.waveforms[1]->getBounds();

    REQUIRE(ch0.y == 0);
    REQUIRE(ch0.y + ch0.h == ch1.y);
    REQUIRE(ch1.y + ch1.h == waveformsBounds.h);
}

TEST_CASE("Transport button spacing is physically stable across scales", "[gui]")
{
    cupuacu::State state{};
    cupuacu::gui::TransportButtonsContainer container(&state);
    container.setBounds(0, 0, 320, 40);

    const auto *play = findByNameRecursive(&container, "TextButton:Play");
    const auto *stop = findByNameRecursive(&container, "TextButton:Stop");
    const auto *loop = findByNameRecursive(&container, "TextButton:Loop");
    REQUIRE(play != nullptr);
    REQUIRE(stop != nullptr);
    REQUIRE(loop != nullptr);

    auto measurePhysical = [&](const uint8_t scale)
    {
        state.pixelScale = scale;
        const int widthVirtual =
            std::max(1, static_cast<int>(std::lround(320.0 / scale)));
        const int heightVirtual =
            std::max(1, static_cast<int>(std::lround(40.0 / scale)));
        container.setBounds(0, 0, widthVirtual, heightVirtual);
        container.resized();
        const SDL_Rect c = container.getBounds();
        const SDL_Rect p = play->getBounds();
        const SDL_Rect s = stop->getBounds();
        const SDL_Rect l = loop->getBounds();
        const int padPhys = p.x * scale;
        const int gapPhys = (s.x - (p.x + p.w)) * scale;
        const int rightPadPhys = (c.w - (l.x + l.w)) * scale;
        const int buttonHPhys = p.h * scale;
        const int containerHPhys = c.h * scale;
        return std::tuple<int, int, int, int, int>{padPhys, gapPhys,
                                              rightPadPhys, buttonHPhys,
                                              containerHPhys};
    };

    const auto [pad1, gap1, rpad1, btnH1, contH1] = measurePhysical(1);
    const auto [pad2, gap2, rpad2, btnH2, contH2] = measurePhysical(2);
    const auto [pad4, gap4, rpad4, btnH4, contH4] = measurePhysical(4);

    REQUIRE(std::abs(pad2 - pad1) <= 2);
    REQUIRE(std::abs(pad4 - pad1) <= 2);
    REQUIRE(std::abs(gap2 - gap1) <= 2);
    REQUIRE(std::abs(gap4 - gap1) <= 2);
    REQUIRE(std::abs(rpad2 - rpad1) <= 4);
    REQUIRE(std::abs(rpad4 - rpad1) <= 6);
    REQUIRE(std::abs(contH2 - contH1) <= 2);
    REQUIRE(std::abs(contH4 - contH1) <= 2);
    REQUIRE(btnH4 > 0);
    REQUIRE(btnH4 >= btnH2 - 2);
}

TEST_CASE("Transport button label centering is stable across scales", "[gui]")
{
    cupuacu::State state{};
    cupuacu::gui::TransportButtonsContainer container(&state);
    container.setBounds(0, 0, 320, 40);

    const auto *playButton = findByNameRecursive(&container, "TextButton:Play");
    REQUIRE(playButton != nullptr);

    auto measureOffset = [&](const uint8_t scale)
    {
        state.pixelScale = scale;
        container.resized();

        const auto *playLabel = findByNameRecursive(playButton, "Label: Play");
        REQUIRE(playLabel != nullptr);

        const SDL_Rect buttonAbs = playButton->getAbsoluteBounds();
        const SDL_Rect labelAbs = playLabel->getAbsoluteBounds();

        const int dx = (labelAbs.x + labelAbs.w / 2) -
                       (buttonAbs.x + buttonAbs.w / 2);
        const int dy = (labelAbs.y + labelAbs.h / 2) -
                       (buttonAbs.y + buttonAbs.h / 2);
        return std::pair<int, int>{dx * scale, dy * scale};
    };

    const auto [dx1, dy1] = measureOffset(1);
    const auto [dx2, dy2] = measureOffset(2);
    const auto [dx4, dy4] = measureOffset(4);

    REQUIRE(std::abs(dx2 - dx1) <= 4);
    REQUIRE(std::abs(dx4 - dx1) <= 4);
    REQUIRE(std::abs(dy2 - dy1) <= 4);
    REQUIRE(std::abs(dy4 - dy1) <= 4);
}
