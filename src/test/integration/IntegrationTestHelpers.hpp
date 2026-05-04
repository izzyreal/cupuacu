#pragma once

#include "../TestSdlTtfGuard.hpp"
#include "../TestPaths.hpp"

#include "State.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/Component.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/EventHandling.hpp"
#include "gui/MainView.hpp"
#include "gui/Menu.hpp"
#include "gui/TextButton.hpp"
#include "gui/Window.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

namespace cupuacu::test::integration
{
    class RootComponent : public cupuacu::gui::Component
    {
    public:
        explicit RootComponent(cupuacu::State *state)
            : Component(state, "Root")
        {
        }
    };

    template <typename T = cupuacu::gui::Component>
    T *findByNameRecursive(cupuacu::gui::Component *root,
                           const std::string_view name)
    {
        if (root == nullptr)
        {
            return nullptr;
        }

        if (root->getComponentName() == name)
        {
            return dynamic_cast<T *>(root);
        }

        for (const auto &child : root->getChildren())
        {
            if (auto *found = findByNameRecursive<T>(child.get(), name))
            {
                return found;
            }
        }

        return nullptr;
    }

    inline std::vector<cupuacu::gui::Menu *>
    menuChildren(cupuacu::gui::Component *parent)
    {
        std::vector<cupuacu::gui::Menu *> result;
        for (const auto &child : parent->getChildren())
        {
            if (auto *menu = dynamic_cast<cupuacu::gui::Menu *>(child.get()))
            {
                result.push_back(menu);
            }
        }
        return result;
    }

    inline cupuacu::gui::MouseEvent leftMouseDown()
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }

    inline cupuacu::gui::MouseEvent leftMouseUp()
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::UP, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }

    inline void clickButton(cupuacu::gui::TextButton *button)
    {
        REQUIRE(button != nullptr);
        REQUIRE(button->mouseDown(leftMouseDown()));
        REQUIRE(button->mouseUp(leftMouseUp()));
        if (auto *window = button->getWindow())
        {
            window->handleMouseEvent(cupuacu::gui::MouseEvent{
                cupuacu::gui::MOVE, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                cupuacu::gui::MouseButtonState{false, false, false}, 0});
        }
    }

    inline void processPendingSdlWindowEvents(cupuacu::State *state)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            cupuacu::gui::handleAppEvent(state, &event);
        }
    }

    inline cupuacu::gui::MouseEvent componentMouseEvent(
        cupuacu::gui::Component *component,
        const cupuacu::gui::MouseEventType type, const uint8_t clicks = 1,
        const bool leftDown = true)
    {
        REQUIRE(component != nullptr);
        const auto bounds = component->getAbsoluteBounds();
        const int x = bounds.x + std::max(1, bounds.w / 2);
        const int y = bounds.y + std::max(1, bounds.h / 2);
        return cupuacu::gui::MouseEvent{
            type,
            x,
            y,
            static_cast<float>(x),
            static_cast<float>(y),
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{leftDown, false, false},
            clicks};
    }

    inline cupuacu::gui::MouseEvent localComponentMouseEvent(
        cupuacu::gui::Component *component,
        const cupuacu::gui::MouseEventType type, const uint8_t clicks = 1,
        const bool leftDown = true)
    {
        REQUIRE(component != nullptr);
        const auto bounds = component->getBounds();
        const int x = std::max(1, bounds.w / 2);
        const int y = std::max(1, bounds.h / 2);
        return cupuacu::gui::MouseEvent{
            type,
            x,
            y,
            static_cast<float>(x),
            static_cast<float>(y),
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{leftDown, false, false},
            clicks};
    }

    inline void clickComponentThroughWindow(cupuacu::gui::Window *window,
                                            cupuacu::gui::Component *component,
                                            const uint8_t clicks = 1)
    {
        REQUIRE(window != nullptr);
        REQUIRE(component != nullptr);
        REQUIRE(window->handleMouseEvent(
            componentMouseEvent(component, cupuacu::gui::DOWN, clicks, true)));
        REQUIRE(window->handleMouseEvent(
            componentMouseEvent(component, cupuacu::gui::UP, clicks, true)));
    }

    inline void moveMouseOverComponent(cupuacu::gui::Window *window,
                                       cupuacu::gui::Component *component)
    {
        REQUIRE(window != nullptr);
        REQUIRE(component != nullptr);
        REQUIRE(window->handleMouseEvent(
            componentMouseEvent(component, cupuacu::gui::MOVE, 0, false)));
    }

    struct SessionUi
    {
        std::unique_ptr<cupuacu::gui::Component> root;
        cupuacu::gui::Component *contentLayer = nullptr;
        cupuacu::gui::MainView *mainView = nullptr;
    };

    inline SessionUi createSessionUi(cupuacu::State *state,
                                     const int64_t frameCount,
                                     const bool withAudioDevices = false,
                                     const int channels = 2,
                                     const int sampleRate = 44100,
                                     const int windowWidth = 800,
                                     const int windowHeight = 400,
                                     const int mainViewHeight = 300)
    {
        ensureSdlTtfInitialized();
        cupuacu::test::ensureTestPaths(*state, "integration-session-ui");

        auto &session = state->getActiveDocumentSession();
        session.document.initialize(cupuacu::SampleFormat::FLOAT32, sampleRate,
                                    channels, frameCount);

        if (withAudioDevices)
        {
            state->audioDevices =
                std::make_shared<cupuacu::audio::AudioDevices>(false);
        }

        state->mainDocumentSessionWindow =
            std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                state, &session, &state->getActiveViewState(), "test", windowWidth, windowHeight,
                SDL_WINDOW_HIDDEN);

        SessionUi ui{};
        ui.root = std::make_unique<RootComponent>(state);
        ui.root->setBounds(0, 0, windowWidth, windowHeight);
        auto contentLayer = std::make_unique<cupuacu::gui::Component>(
            state, "ContentLayer");
        contentLayer->setInterceptMouseEnabled(false);
        ui.contentLayer = ui.root->addChild(contentLayer);
        auto mainView = std::make_unique<cupuacu::gui::MainView>(state);
        mainView->setBounds(0, 0, windowWidth, mainViewHeight);
        ui.mainView = ui.contentLayer->addChild(mainView);
        state->mainDocumentSessionWindow->getWindow()->setRootComponent(
            std::move(ui.root));
        state->mainDocumentSessionWindow->getWindow()->setContentLayer(
            ui.contentLayer);
        return ui;
    }
} // namespace cupuacu::test::integration
