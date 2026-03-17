#pragma once

#include "../TestSdlTtfGuard.hpp"

#include "State.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/Component.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/MainView.hpp"
#include "gui/Menu.hpp"

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

    struct SessionUi
    {
        std::unique_ptr<cupuacu::gui::MainView> mainView;
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

        auto &session = state->activeDocumentSession;
        session.document.initialize(cupuacu::SampleFormat::FLOAT32, sampleRate,
                                    channels, frameCount);

        if (withAudioDevices)
        {
            state->audioDevices =
                std::make_shared<cupuacu::audio::AudioDevices>(false);
        }

        state->mainDocumentSessionWindow =
            std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                state, &session, "test", windowWidth, windowHeight,
                SDL_WINDOW_HIDDEN);

        SessionUi ui{};
        ui.mainView = std::make_unique<cupuacu::gui::MainView>(state);
        state->mainView = ui.mainView.get();
        ui.mainView->setWindow(state->mainDocumentSessionWindow->getWindow());
        ui.mainView->setBounds(0, 0, windowWidth, mainViewHeight);
        return ui;
    }
} // namespace cupuacu::test::integration
