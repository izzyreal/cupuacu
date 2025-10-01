#include <catch2/catch_test_macros.hpp>

#include "gui/Component.h"

#include <algorithm>

// helper to check if a rect exists in a vector
static bool contains_rect(const std::vector<SDL_Rect> &vec, const SDL_Rect &r)
{
    return std::any_of(vec.begin(), vec.end(),
                       [&](const SDL_Rect &e) {
                           return e.x == r.x &&
                                  e.y == r.y &&
                                  e.w == r.w &&
                                  e.h == r.h;
                       });
}

// helper to construct SDL_Rect easily
static SDL_Rect makeRect(int x, int y, int w, int h)
{
    return SDL_Rect{ x, y, w, h };
}

// Minimal concrete test components
struct DummyA : public Component {
    DummyA(CupuacuState* s, const std::string &name) : Component(s, name) {}
};
struct DummyB : public Component {
    DummyB(CupuacuState* s, const std::string &name) : Component(s, name) {}
};

TEST_CASE("parent setDirty marks itself and children only (no ancestors or siblings)", "[dirty]")
{
    CupuacuState state{};
    state.componentUnderMouse = nullptr;
    state.capturingComponent = nullptr;
    state.menuBar = nullptr;

    Component root(&state, "root");
    root.setBounds(0,0,200,200);

    Component* a  = root.emplaceChild<Component>(&state, "A");
    a->setBounds(10,10,100,100);

    Component* a1 = a->emplaceChild<Component>(&state, "A1");
    a1->setBounds(5,5,30,30);

    Component* a2 = a->emplaceChild<Component>(&state, "A2");
    a2->setBounds(40,40,30,30);

    Component* b  = root.emplaceChild<Component>(&state, "B");
    b->setBounds(120,10,50,50);

    state.dirtyRects.clear();

    a->setDirty();

    REQUIRE(contains_rect(state.dirtyRects, a->getAbsoluteBounds()));
    REQUIRE(contains_rect(state.dirtyRects, a1->getAbsoluteBounds()));
    REQUIRE(contains_rect(state.dirtyRects, a2->getAbsoluteBounds()));
    REQUIRE(!contains_rect(state.dirtyRects, root.getAbsoluteBounds()));
    REQUIRE(!contains_rect(state.dirtyRects, b->getAbsoluteBounds()));
}

TEST_CASE("setBounds records old and new bounds", "[bounds]")
{
    CupuacuState state{};
    state.menuBar = nullptr;
    state.componentUnderMouse = nullptr;
    state.capturingComponent = nullptr;

    Component c(&state, "comp");
    c.setBounds(0,0,20,20);

    state.dirtyRects.clear();

    c.setBounds(5,5,30,30);

    SDL_Rect oldRect = makeRect(0,0,20,20);
    SDL_Rect newRect = makeRect(5,5,30,30);

    REQUIRE(state.dirtyRects.size() >= 2);
    REQUIRE(contains_rect(state.dirtyRects, oldRect));
    REQUIRE(contains_rect(state.dirtyRects, newRect));
}

TEST_CASE("removeChild pushes old child's bounds into dirtyRects", "[removeChild]")
{
    CupuacuState state{};
    state.menuBar = nullptr;
    state.componentUnderMouse = nullptr;
    state.capturingComponent = nullptr;

    Component root(&state, "root");
    root.setBounds(0,0,300,300);

    Component* child = root.emplaceChild<Component>(&state, "child");
    child->setBounds(10, 20, 50, 40);

    state.dirtyRects.clear();

    SDL_Rect oldBounds = child->getAbsoluteBounds();
    root.removeChild(child);

    REQUIRE(contains_rect(state.dirtyRects, oldBounds));
}

TEST_CASE("sendToBack and bringToFront mark parent dirty and reorder children", "[zorder]")
{
    CupuacuState state{};
    state.menuBar = nullptr;
    state.componentUnderMouse = nullptr;
    state.capturingComponent = nullptr;

    Component root(&state, "root");
    root.setBounds(0,0,500,500);

    Component* c1 = root.emplaceChild<Component>(&state, "c1");
    c1->setBounds(0,0,10,10);

    Component* c2 = root.emplaceChild<Component>(&state, "c2");
    c2->setBounds(20,0,10,10);

    state.dirtyRects.clear();

    c2->sendToBack();
    REQUIRE(contains_rect(state.dirtyRects, root.getAbsoluteBounds()));

    state.dirtyRects.clear();
    c2->bringToFront();
    REQUIRE(contains_rect(state.dirtyRects, root.getAbsoluteBounds()));

    auto &children = root.getChildren();
    REQUIRE(children.back().get() == c2);
}

TEST_CASE("removeChildrenOfType removes matching children and sets dirty", "[removeChildrenOfType]")
{
    CupuacuState state{};
    state.menuBar = nullptr;
    state.componentUnderMouse = nullptr;
    state.capturingComponent = nullptr;

    Component root(&state, "root");
    root.setBounds(0,0,200,200);

    DummyA* a1 = root.emplaceChild<DummyA>(&state, "a1");
    a1->setBounds(0,0,10,10);

    DummyA* a2 = root.emplaceChild<DummyA>(&state, "a2");
    a2->setBounds(15,0,10,10);

    DummyB* b1 = root.emplaceChild<DummyB>(&state, "b1");
    b1->setBounds(30,0,10,10);

    state.dirtyRects.clear();

    root.removeChildrenOfType<DummyA>();

    REQUIRE(contains_rect(state.dirtyRects, root.getAbsoluteBounds()));

    const auto &children = root.getChildren();
    REQUIRE(children.size() == 1);
    REQUIRE(children.front()->getComponentName() == "b1");
}

TEST_CASE("containsAbsoluteCoordinate respects parent clipping and disableParentClipping", "[clipping]")
{
    CupuacuState state{};
    state.menuBar = nullptr;
    state.componentUnderMouse = nullptr;
    state.capturingComponent = nullptr;

    Component root(&state, "root");
    root.setBounds(0,0,50,50);

    Component* parent = root.emplaceChild<Component>(&state, "parent");
    parent->setBounds(10,10,20,20);

    Component* child = parent->emplaceChild<Component>(&state, "child");
    child->setBounds(-5,-5,40,40);

    state.dirtyRects.clear();

    int testX = 8;
    int testY = 8;

    REQUIRE(!child->containsAbsoluteCoordinate(testX, testY));

    child->disableParentClipping();

    REQUIRE(child->containsAbsoluteCoordinate(testX, testY));
}

