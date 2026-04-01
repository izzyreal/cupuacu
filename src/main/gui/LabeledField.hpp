#pragma once

#include "gui/Component.hpp"
#include "gui/text.hpp"
#include "gui/Helpers.hpp"
#include "gui/TextInput.hpp"
#include "gui/UiScale.hpp"

#include "State.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace cupuacu::gui
{
    class LabeledField : public Component
    {
    private:
        std::string label;
        std::string value;
        const SDL_Color background;
        bool editable = false;
        bool editing = false;
        std::string editValueBeforeSession;
        TextInput *editor = nullptr;
        std::function<bool(const std::string &)> onSubmit;

        float getContentInset() const
        {
            return std::max(
                0.0f, std::round((static_cast<float>(getHeight()) -
                                  static_cast<float>(measureText(
                                      label + ": " + std::max(value, std::string("0")),
                                      scaleFontPointSize(state, state->menuFontSize))
                                      .second)) /
                                 2.0f));
        }

        void updateEditorBounds()
        {
            if (!editor)
            {
                return;
            }

            const uint8_t fontPointSize =
                scaleFontPointSize(state, state->menuFontSize);
            const float inset = getContentInset();
            const int labelWidth =
                measureText(label + ": ", fontPointSize).first;
            const int x = std::min(
                getWidth(), static_cast<int>(std::round(inset)) + labelWidth);
            const int y = static_cast<int>(std::round(inset));
            const int h = std::max(0, getHeight() - y * 2);
            const int w = std::max(0, getWidth() - x - y);
            editor->setBounds(x, y, w, h);
        }

        void stopEditing()
        {
            if (!editing)
            {
                return;
            }

            editing = false;
            if (window && window->getFocusedComponent() == editor)
            {
                window->setFocusedComponent(nullptr);
            }
            if (editor)
            {
                editor->setVisible(false);
            }
            setDirty();
        }

    public:
        LabeledField(State *stateToUse, const std::string &labelToUse,
                     const SDL_Color backgroundToUse)
            : Component(stateToUse, "LabeledField for " + labelToUse),
              label(labelToUse), background(backgroundToUse)
        {
            editor = emplaceChild<TextInput>(state);
            editor->setVisible(false);
            editor->setSubmitOnFocusLost(false);
            editor->setConsumeEnterKey(true);
            editor->setOnEditingFinished(
                [this](const std::string &text)
                {
                    const bool accepted =
                        !onSubmit || onSubmit(text);
                    value = accepted ? text : editValueBeforeSession;
                    stopEditing();
                });
            editor->setOnEditingCanceled(
                [this]()
                {
                    value = editValueBeforeSession;
                    stopEditing();
                });
        }

        void setValue(const std::string &newValue)
        {
            if (value != newValue)
            {
                value = newValue;
                if (editing && editor)
                {
                    editor->setText(value);
                    editor->selectAll();
                }
                setDirty();
            }
        }

        const std::string &getValue() const
        {
            return value;
        }

        void setEditable(const bool shouldBeEditable)
        {
            editable = shouldBeEditable;
        }

        bool isEditing() const
        {
            return editing;
        }

        void setAllowedCharacters(const std::string &charactersToUse)
        {
            if (editor)
            {
                editor->setAllowedCharacters(charactersToUse);
            }
        }

        void setOnSubmit(std::function<bool(const std::string &)> callback)
        {
            onSubmit = std::move(callback);
        }

        void resized() override
        {
            updateEditorBounds();
        }

        bool mouseDown(const MouseEvent &event) override
        {
            if (!editable || editing || event.numClicks < 2 || !window)
            {
                return false;
            }

            editing = true;
            editValueBeforeSession = value;
            editor->setText(value);
            editor->selectAll();
            editor->setVisible(true);
            updateEditorBounds();
            window->setFocusedComponent(editor);
            setDirty();
            return true;
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), background);
            const uint8_t fontPointSize =
                scaleFontPointSize(state, state->menuFontSize);
            const std::string displayText = label + ": " + value;
            const auto [textW, textH] = measureText(displayText, fontPointSize);
            auto rect = getLocalBoundsF();
            const float inset = std::max(
                0.0f, std::round((rect.h - static_cast<float>(textH)) / 2.0f));
            rect.x += inset;
            rect.y += inset;
            rect.w = std::max(0.0f, rect.w - (inset * 2.0f));
            rect.h = std::max(0.0f, rect.h - (inset * 2.0f));
            if (editing)
            {
                renderText(renderer, label + ":", fontPointSize, rect, false);
                return;
            }

            renderText(renderer, displayText, fontPointSize, rect, false);
        }
    };
} // namespace cupuacu::gui
