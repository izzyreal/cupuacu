#pragma once

#include "Component.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace cupuacu::gui
{
    class ScrollBar;

    class SelectableTextView : public Component
    {
    public:
        explicit SelectableTextView(State *stateToUse);

        void setText(std::string textToUse);
        const std::string &getText() const
        {
            return text;
        }
        void setFontSize(int pointSizeToUse);
        void selectAll();
        void clearSelection();
        bool hasSelection() const;
        std::string selectedText() const;
        bool copySelection() const;
        bool copyAll() const;
        std::string linkAtTextIndex(std::size_t index) const;

        double getScrollOffset() const
        {
            return scrollOffset;
        }
        double getMaximumScrollOffset() const;
        void setScrollOffset(double offset);

        bool acceptsKeyboardFocus() const override
        {
            return true;
        }
        void focusGained() override;
        void focusLost() override;
        bool mouseDown(const MouseEvent &event) override;
        bool mouseMove(const MouseEvent &event) override;
        bool mouseUp(const MouseEvent &event) override;
        bool mouseWheel(const MouseEvent &event) override;
        bool keyDown(const SDL_KeyboardEvent &event) override;
        void onDraw(SDL_Renderer *renderer) override;
        bool isOpaque() const override
        {
            return false;
        }
        void resized() override;

    private:
        struct VisualLine
        {
            std::size_t start = 0;
            std::size_t length = 0;
        };

        struct LinkSpan
        {
            std::size_t start = 0;
            std::size_t length = 0;
            std::string url;
        };

        std::string text;
        std::vector<VisualLine> lines;
        std::vector<LinkSpan> links;
        ScrollBar *scrollBar = nullptr;
        int fontSize = 18;
        int lineHeight = 1;
        double scrollOffset = 0.0;
        std::size_t selectionAnchor = 0;
        std::size_t selectionCaret = 0;
        std::string pressedLink;
        std::size_t pointerDownTextIndex = 0;
        bool focused = false;
        bool mouseSelecting = false;
        bool pointerDragged = false;

        int padding() const;
        int scrollBarWidth() const;
        int textViewportWidth() const;
        int textViewportHeight() const;
        uint8_t effectiveFontSize() const;
        void rebuildLinks();
        void rebuildLines();
        std::size_t textIndexAt(int x, int y) const;
        std::size_t lineIndexForTextIndex(std::size_t index) const;
        std::size_t nearestIndexOnLine(std::size_t lineIndex,
                                       int targetX) const;
        int textWidth(std::size_t start, std::size_t length) const;
        void moveCaretHorizontally(bool right, bool extend);
        void moveCaretVertically(int direction, bool extend);
        void moveCaretTo(std::size_t index, bool extend);
        void ensureCaretVisible();
        void selectWordAt(std::size_t index);
    };
} // namespace cupuacu::gui
