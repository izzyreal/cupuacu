#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Label.hpp"
#include "gui/Ruler.hpp"
#include "gui/Timeline.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformRefresh.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace
{
    template <typename T>
    T *findByTypeRecursive(cupuacu::gui::Component *root)
    {
        if (root == nullptr)
        {
            return nullptr;
        }

        if (auto *match = dynamic_cast<T *>(root))
        {
            return match;
        }

        for (const auto &child : root->getChildren())
        {
            if (auto *match = findByTypeRecursive<T>(child.get()))
            {
                return match;
            }
        }

        return nullptr;
    }

    std::vector<std::string> getLabelTexts(cupuacu::gui::Ruler *ruler)
    {
        std::vector<std::string> texts;
        for (const auto &child : ruler->getChildren())
        {
            if (auto *label = dynamic_cast<cupuacu::gui::Label *>(child.get()))
            {
                texts.push_back(label->getText());
            }
        }
        return texts;
    }

    std::vector<std::string> getNumericTexts(cupuacu::gui::Ruler *ruler)
    {
        std::vector<std::string> texts;
        for (const auto &text : getLabelTexts(ruler))
        {
            if (!text.empty() &&
                std::all_of(text.begin(), text.end(),
                            [](const unsigned char ch)
                            { return std::isdigit(ch) != 0; }))
            {
                texts.push_back(text);
            }
        }
        return texts;
    }
} // namespace

TEST_CASE("Timeline sample labels shift when sample offset crosses a tick boundary",
          "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 10000, false, 2, 44100, 800,
                                             400, 300);

    auto *timeline = findByTypeRecursive<cupuacu::gui::Timeline>(ui.mainView.get());
    auto *ruler = findByTypeRecursive<cupuacu::gui::Ruler>(timeline);
    REQUIRE(timeline != nullptr);
    REQUIRE(ruler != nullptr);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 1.0;
    viewState.sampleOffset = 0;
    timeline->updateLabels();
    const auto initialTexts = getNumericTexts(ruler);

    viewState.sampleOffset = 101;
    timeline->timerCallback();
    const auto shiftedTexts = getNumericTexts(ruler);

    REQUIRE_FALSE(initialTexts.empty());
    REQUIRE_FALSE(shiftedTexts.empty());
    REQUIRE(std::find(initialTexts.begin(), initialTexts.end(), "100") !=
            initialTexts.end());
    REQUIRE(std::find(shiftedTexts.begin(), shiftedTexts.end(), "100") ==
            shiftedTexts.end());
    REQUIRE(initialTexts != shiftedTexts);
}

TEST_CASE("Timeline decimal mode emits time-formatted labels", "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 200000, false, 2, 48000, 800,
                                             400, 300);

    auto *timeline = findByTypeRecursive<cupuacu::gui::Timeline>(ui.mainView.get());
    auto *ruler = findByTypeRecursive<cupuacu::gui::Ruler>(timeline);
    REQUIRE(timeline != nullptr);
    REQUIRE(ruler != nullptr);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 100.0;
    viewState.sampleOffset = 48000;
    timeline->setMode(cupuacu::gui::Timeline::Mode::Decimal);
    timeline->updateLabels();

    const auto texts = getLabelTexts(ruler);
    REQUIRE(std::find(texts.begin(), texts.end(), "smpl") != texts.end());
    REQUIRE(std::any_of(texts.begin(), texts.end(),
                        [](const std::string &text)
                        { return text.find(':') != std::string::npos; }));
}

TEST_CASE("refreshWaveforms only updates sample points when requested", "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 128, false, 2);
    REQUIRE(state.waveforms.size() == 2);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.01;
    cupuacu::gui::refreshWaveforms(&state, true, false);
    REQUIRE_FALSE(state.waveforms[0]->getChildren().empty());
    REQUIRE_FALSE(state.waveforms[1]->getChildren().empty());

    viewState.samplesPerPixel = 1.0;
    cupuacu::gui::refreshWaveforms(&state, false, false);
    REQUIRE_FALSE(state.waveforms[0]->getChildren().empty());
    REQUIRE_FALSE(state.waveforms[1]->getChildren().empty());

    cupuacu::gui::refreshWaveforms(&state, true, false);
    REQUIRE(state.waveforms[0]->getChildren().empty());
    REQUIRE(state.waveforms[1]->getChildren().empty());
}

TEST_CASE("clearWaveformHighlights clears highlighted sample positions on all channels",
          "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 64, false, 2);
    REQUIRE(state.waveforms.size() == 2);

    state.waveforms[0]->setSamplePosUnderCursor(5);
    state.waveforms[1]->setSamplePosUnderCursor(9);
    REQUIRE(state.waveforms[0]->getSamplePosUnderCursor() == 5);
    REQUIRE(state.waveforms[1]->getSamplePosUnderCursor() == 9);

    cupuacu::gui::clearWaveformHighlights(&state);

    REQUIRE_FALSE(state.waveforms[0]->getSamplePosUnderCursor().has_value());
    REQUIRE_FALSE(state.waveforms[1]->getSamplePosUnderCursor().has_value());
}
