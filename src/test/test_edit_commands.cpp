#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "actions/audio/EditCommands.hpp"
#include "gui/DevicePropertiesWindow.hpp"

TEST_CASE("Edit command helpers derive selection and paste targets", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    SECTION("selection target inactive")
    {
        session.selection.reset();
        const auto target = cupuacu::actions::audio::selectionTarget(&state);
        REQUIRE(target.start == 0);
        REQUIRE(target.length == 0);
    }

    SECTION("selection target active")
    {
        session.selection.setValue1(11.0);
        session.selection.setValue2(19.0); // [11, 19)
        const auto target = cupuacu::actions::audio::selectionTarget(&state);
        REQUIRE(target.start == 11);
        REQUIRE(target.length == 8);
    }

    SECTION("paste target without selection uses cursor")
    {
        session.selection.reset();
        session.cursor = 33;
        const auto target = cupuacu::actions::audio::pasteTarget(&state);
        REQUIRE(target.start == 33);
        REQUIRE(target.end == -1);
    }

    SECTION("paste target with selection uses selection bounds")
    {
        session.selection.setValue1(7.0);
        session.selection.setValue2(15.0); // [7, 15)
        const auto target = cupuacu::actions::audio::pasteTarget(&state);
        REQUIRE(target.start == 7);
        REQUIRE(target.end == 14);
    }
}
