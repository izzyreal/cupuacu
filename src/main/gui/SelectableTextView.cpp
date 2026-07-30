#include "SelectableTextView.hpp"

#include "Colors.hpp"
#include "Helpers.hpp"
#include "ScrollBar.hpp"
#include "UiScale.hpp"
#include "Window.hpp"
#include "text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>
#include <utility>

namespace
{
    constexpr SDL_Color kPanelFill{22, 22, 22, 210};
    constexpr SDL_Color kSelectionFill{82, 116, 172, 220};
    constexpr SDL_Color kLinkUnderline{116, 166, 235, 255};

    bool primaryModifierHeld(const SDL_KeyboardEvent &event)
    {
#if __APPLE__
        return (event.mod & SDL_KMOD_GUI) != 0;
#else
        return (event.mod & SDL_KMOD_CTRL) != 0;
#endif
    }

    bool isUtf8Continuation(const unsigned char value)
    {
        return (value & 0xc0U) == 0x80U;
    }

    std::size_t nextCodePoint(const std::string &text, std::size_t index)
    {
        index = std::min(index + 1, text.size());
        while (index < text.size() &&
               isUtf8Continuation(static_cast<unsigned char>(text[index])))
        {
            ++index;
        }
        return index;
    }

    std::size_t previousCodePoint(const std::string &text, std::size_t index)
    {
        if (index == 0)
        {
            return 0;
        }
        --index;
        while (index > 0 &&
               isUtf8Continuation(static_cast<unsigned char>(text[index])))
        {
            --index;
        }
        return index;
    }

    bool isWordCharacter(const char value)
    {
        const unsigned char c = static_cast<unsigned char>(value);
        return c >= 0x80U || std::isalnum(c) != 0 || c == '_';
    }

    bool isUrlTerminator(const char value)
    {
        return std::isspace(static_cast<unsigned char>(value)) != 0;
    }

    bool shouldTrimFromUrlEnd(const char value)
    {
        constexpr std::string_view punctuation{".,;:!?)]}\"'"};
        return punctuation.find(value) != std::string_view::npos;
    }
} // namespace

using namespace cupuacu::gui;

SelectableTextView::SelectableTextView(State *stateToUse)
    : Component(stateToUse, "SelectableTextView")
{
    fontSize =
        state ? std::max(12, static_cast<int>(state->menuFontSize) - 10) : 18;
    scrollBar = emplaceChild<ScrollBar>(
        state, ScrollBar::Orientation::Vertical,
        [this]()
        {
            return scrollOffset;
        },
        []()
        {
            return 0.0;
        },
        [this]()
        {
            return getMaximumScrollOffset();
        },
        [this]()
        {
            return static_cast<double>(textViewportHeight());
        },
        [this](const double value)
        {
            setScrollOffset(value);
        });
}

void SelectableTextView::setText(std::string textToUse)
{
    if (text == textToUse)
    {
        return;
    }
    text = std::move(textToUse);
    selectionAnchor = 0;
    selectionCaret = 0;
    scrollOffset = 0.0;
    rebuildLinks();
    rebuildLines();
    setDirty();
}

void SelectableTextView::setFontSize(const int pointSizeToUse)
{
    const int next = std::max(1, pointSizeToUse);
    if (fontSize == next)
    {
        return;
    }
    fontSize = next;
    rebuildLines();
    setDirty();
}

void SelectableTextView::selectAll()
{
    selectionAnchor = 0;
    selectionCaret = text.size();
    ensureCaretVisible();
    setDirty();
}

void SelectableTextView::clearSelection()
{
    selectionAnchor = selectionCaret;
    setDirty();
}

bool SelectableTextView::hasSelection() const
{
    return selectionAnchor != selectionCaret;
}

std::string SelectableTextView::selectedText() const
{
    const std::size_t start = std::min(selectionAnchor, selectionCaret);
    const std::size_t end = std::max(selectionAnchor, selectionCaret);
    return text.substr(start, end - start);
}

bool SelectableTextView::copySelection() const
{
    if (!hasSelection())
    {
        return false;
    }
    return SDL_SetClipboardText(selectedText().c_str());
}

bool SelectableTextView::copyAll() const
{
    return SDL_SetClipboardText(text.c_str());
}

std::string SelectableTextView::linkAtTextIndex(const std::size_t index) const
{
    const auto link = std::find_if(links.begin(), links.end(),
                                   [index](const LinkSpan &span)
                                   {
                                       return index >= span.start &&
                                              index < span.start + span.length;
                                   });
    return link == links.end() ? std::string{} : link->url;
}

double SelectableTextView::getMaximumScrollOffset() const
{
    const double contentHeight = static_cast<double>(
        lines.size() * static_cast<std::size_t>(lineHeight));
    return std::max(0.0, contentHeight - textViewportHeight());
}

void SelectableTextView::setScrollOffset(const double offset)
{
    const double next = std::clamp(offset, 0.0, getMaximumScrollOffset());
    if (std::abs(next - scrollOffset) < 0.01)
    {
        return;
    }
    scrollOffset = next;
    setDirty();
    if (scrollBar)
    {
        scrollBar->setDirty();
    }
}

void SelectableTextView::focusGained()
{
    focused = true;
}

void SelectableTextView::focusLost()
{
    focused = false;
    mouseSelecting = false;
    pressedLink.clear();
    pointerDragged = false;
}

bool SelectableTextView::mouseDown(const MouseEvent &event)
{
    if (!event.buttonState.left)
    {
        return false;
    }
    if (window)
    {
        window->setFocusedComponent(this);
    }

    const std::size_t index = textIndexAt(event.mouseXi, event.mouseYi);
    pointerDownTextIndex = index;
    pointerDragged = false;
    pressedLink = event.numClicks <= 1 && (event.mod & SDL_KMOD_SHIFT) == 0
                      ? linkAtTextIndex(index)
                      : std::string{};
    if (event.numClicks >= 3)
    {
        selectAll();
    }
    else if (event.numClicks == 2)
    {
        selectWordAt(index);
    }
    else
    {
        selectionCaret = index;
        if ((event.mod & SDL_KMOD_SHIFT) == 0)
        {
            selectionAnchor = index;
        }
    }
    mouseSelecting = true;
    setDirty();
    return true;
}

bool SelectableTextView::mouseMove(const MouseEvent &event)
{
    if (!mouseSelecting || !event.buttonState.left)
    {
        return false;
    }
    const std::size_t index = textIndexAt(event.mouseXi, event.mouseYi);
    pointerDragged = pointerDragged || index != pointerDownTextIndex;
    selectionCaret = index;
    ensureCaretVisible();
    setDirty();
    return true;
}

bool SelectableTextView::mouseUp(const MouseEvent &)
{
    if (!mouseSelecting)
    {
        return false;
    }
    mouseSelecting = false;
    const std::string linkToOpen =
        pointerDragged ? std::string{} : std::move(pressedLink);
    pressedLink.clear();
    pointerDragged = false;
    if (!linkToOpen.empty())
    {
        (void)SDL_OpenURL(linkToOpen.c_str());
    }
    return true;
}

bool SelectableTextView::mouseWheel(const MouseEvent &event)
{
    if (event.wheelY == 0.0f)
    {
        return false;
    }
    setScrollOffset(scrollOffset -
                    event.wheelY * static_cast<double>(lineHeight) * 3.0);
    return true;
}

bool SelectableTextView::keyDown(const SDL_KeyboardEvent &event)
{
    if (!focused)
    {
        return false;
    }

    const bool extend = (event.mod & SDL_KMOD_SHIFT) != 0;
    if (primaryModifierHeld(event))
    {
        if (event.scancode == SDL_SCANCODE_A)
        {
            selectAll();
            return true;
        }
        if (event.scancode == SDL_SCANCODE_C)
        {
            (void)copySelection();
            return true;
        }
    }

    switch (event.scancode)
    {
        case SDL_SCANCODE_LEFT:
            moveCaretHorizontally(false, extend);
            return true;
        case SDL_SCANCODE_RIGHT:
            moveCaretHorizontally(true, extend);
            return true;
        case SDL_SCANCODE_UP:
            moveCaretVertically(-1, extend);
            return true;
        case SDL_SCANCODE_DOWN:
            moveCaretVertically(1, extend);
            return true;
        case SDL_SCANCODE_HOME:
        {
            const auto &line = lines[lineIndexForTextIndex(selectionCaret)];
            moveCaretTo(line.start, extend);
            return true;
        }
        case SDL_SCANCODE_END:
        {
            const auto &line = lines[lineIndexForTextIndex(selectionCaret)];
            moveCaretTo(line.start + line.length, extend);
            return true;
        }
        case SDL_SCANCODE_PAGEUP:
            setScrollOffset(scrollOffset - textViewportHeight());
            return true;
        case SDL_SCANCODE_PAGEDOWN:
            setScrollOffset(scrollOffset + textViewportHeight());
            return true;
        default:
            return false;
    }
}

void SelectableTextView::onDraw(SDL_Renderer *renderer)
{
    Helpers::fillRect(renderer, getLocalBounds(), kPanelFill);

    if (lines.empty() || lineHeight <= 0)
    {
        return;
    }

    const int firstLine =
        std::max(0, static_cast<int>(scrollOffset) / lineHeight);
    const int lastLine = std::min(
        static_cast<int>(lines.size()),
        firstLine + std::max(1, textViewportHeight() / lineHeight + 2));
    const std::size_t selectionStart =
        std::min(selectionAnchor, selectionCaret);
    const std::size_t selectionEnd = std::max(selectionAnchor, selectionCaret);

    for (int index = firstLine; index < lastLine; ++index)
    {
        const auto &line = lines[static_cast<std::size_t>(index)];
        const int y = padding() + index * lineHeight -
                      static_cast<int>(std::floor(scrollOffset));
        const std::size_t lineEnd = line.start + line.length;
        const std::size_t selectedStart = std::max(selectionStart, line.start);
        const std::size_t selectedEnd = std::min(selectionEnd, lineEnd);
        if (selectedStart < selectedEnd)
        {
            const int x =
                padding() + textWidth(line.start, selectedStart - line.start);
            const int width =
                textWidth(selectedStart, selectedEnd - selectedStart);
            Helpers::fillRect(renderer, SDL_Rect{x, y, width, lineHeight},
                              kSelectionFill);
        }

        const std::string lineText = text.substr(line.start, line.length);
        renderText(renderer, lineText, effectiveFontSize(),
                   SDL_FRect{static_cast<float>(padding()),
                             static_cast<float>(y),
                             static_cast<float>(textViewportWidth()),
                             static_cast<float>(lineHeight)},
                   false);

        for (const auto &link : links)
        {
            const std::size_t linkStart = link.start;
            const std::size_t linkEnd = link.start + link.length;
            const std::size_t visibleStart = std::max(linkStart, line.start);
            const std::size_t visibleEnd = std::min(linkEnd, lineEnd);
            if (visibleStart >= visibleEnd)
            {
                continue;
            }

            const int x =
                padding() + textWidth(line.start, visibleStart - line.start);
            const int width =
                textWidth(visibleStart, visibleEnd - visibleStart);
            const int textHeight =
                measureText("Ag", effectiveFontSize()).second;
            Helpers::fillRect(
                renderer,
                SDL_Rect{x, y + std::min(lineHeight - 1, textHeight), width,
                         std::max(1, scaleUi(state, 1.0f))},
                kLinkUnderline);
        }
    }
}

void SelectableTextView::resized()
{
    if (scrollBar)
    {
        scrollBar->setBounds(std::max(0, getWidth() - scrollBarWidth()), 0,
                             scrollBarWidth(), getHeight());
    }
    rebuildLines();
}

int SelectableTextView::padding() const
{
    return scaleUi(state, 12.0f);
}

int SelectableTextView::scrollBarWidth() const
{
    return scaleUi(state, 14.0f);
}

int SelectableTextView::textViewportWidth() const
{
    return std::max(1, getWidth() - padding() * 2 - scrollBarWidth());
}

int SelectableTextView::textViewportHeight() const
{
    return std::max(1, getHeight() - padding() * 2);
}

uint8_t SelectableTextView::effectiveFontSize() const
{
    return scaleFontPointSize(state, fontSize);
}

void SelectableTextView::rebuildLinks()
{
    links.clear();
    std::size_t searchStart = 0;
    while (searchStart < text.size())
    {
        const std::size_t http = text.find("http://", searchStart);
        const std::size_t https = text.find("https://", searchStart);
        const std::size_t start = std::min(http, https);
        if (start == std::string::npos)
        {
            break;
        }

        std::size_t end = start;
        while (end < text.size() && !isUrlTerminator(text[end]))
        {
            ++end;
        }
        while (end > start && shouldTrimFromUrlEnd(text[end - 1]))
        {
            --end;
        }

        if (end > start)
        {
            links.push_back(
                {start, end - start, text.substr(start, end - start)});
        }
        searchStart = std::max(start + 1, end);
    }
}

void SelectableTextView::rebuildLines()
{
    lines.clear();
    lineHeight =
        std::max(1, static_cast<int>(std::ceil(
                        measureText("Ag", effectiveFontSize()).second * 1.3)));

    if (text.empty())
    {
        lines.push_back({0, 0});
        setScrollOffset(0.0);
        return;
    }

    const int maxWidth = textViewportWidth();
    std::size_t paragraphStart = 0;
    while (paragraphStart <= text.size())
    {
        const std::size_t newline = text.find('\n', paragraphStart);
        const std::size_t paragraphEnd =
            newline == std::string::npos ? text.size() : newline;
        if (paragraphStart == paragraphEnd)
        {
            lines.push_back({paragraphStart, 0});
        }
        else
        {
            std::size_t lineStart = paragraphStart;
            TTF_Font *font = getFont(effectiveFontSize());
            while (lineStart < paragraphEnd)
            {
                std::size_t measuredLength = 0;
                int measuredWidth = 0;
                if (font)
                {
                    (void)TTF_MeasureString(font, text.data() + lineStart,
                                            paragraphEnd - lineStart, maxWidth,
                                            &measuredWidth, &measuredLength);
                }
                if (measuredLength == 0)
                {
                    measuredLength = nextCodePoint(text, lineStart) - lineStart;
                }

                std::size_t lineEnd =
                    std::min(paragraphEnd, lineStart + measuredLength);
                if (lineEnd < paragraphEnd)
                {
                    std::size_t breakPosition = lineEnd;
                    while (breakPosition > lineStart &&
                           std::isspace(static_cast<unsigned char>(
                               text[breakPosition - 1])) == 0)
                    {
                        --breakPosition;
                    }
                    if (breakPosition > lineStart)
                    {
                        lineEnd = breakPosition;
                    }
                }
                lines.push_back({lineStart, lineEnd - lineStart});
                lineStart = lineEnd;
            }
        }

        if (newline == std::string::npos)
        {
            break;
        }
        paragraphStart = newline + 1;
    }

    selectionAnchor = std::min(selectionAnchor, text.size());
    selectionCaret = std::min(selectionCaret, text.size());
    setScrollOffset(scrollOffset);
}

std::size_t SelectableTextView::textIndexAt(const int x, const int y) const
{
    if (lines.empty())
    {
        return 0;
    }
    const int contentY =
        y - padding() + static_cast<int>(std::floor(scrollOffset));
    const std::size_t lineIndex = static_cast<std::size_t>(
        std::clamp(contentY / std::max(1, lineHeight), 0,
                   static_cast<int>(lines.size()) - 1));
    return nearestIndexOnLine(lineIndex, std::max(0, x - padding()));
}

std::size_t
SelectableTextView::lineIndexForTextIndex(const std::size_t index) const
{
    if (lines.empty())
    {
        return 0;
    }
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const auto &line = lines[lineIndex];
        if (index <= line.start + line.length)
        {
            return lineIndex;
        }
    }
    return lines.size() - 1;
}

std::size_t SelectableTextView::nearestIndexOnLine(const std::size_t lineIndex,
                                                   const int targetX) const
{
    const auto &line = lines[std::min(lineIndex, lines.size() - 1)];
    std::size_t best = line.start;
    int bestDistance = std::abs(targetX);
    std::size_t index = line.start;
    const std::size_t end = line.start + line.length;
    while (index < end)
    {
        index = nextCodePoint(text, index);
        const int distance =
            std::abs(targetX - textWidth(line.start, index - line.start));
        if (distance < bestDistance)
        {
            best = index;
            bestDistance = distance;
        }
    }
    return best;
}

int SelectableTextView::textWidth(const std::size_t start,
                                  const std::size_t length) const
{
    return measureText(text.substr(start, length), effectiveFontSize()).first;
}

void SelectableTextView::moveCaretHorizontally(const bool right,
                                               const bool extend)
{
    if (!extend && hasSelection())
    {
        moveCaretTo(right ? std::max(selectionAnchor, selectionCaret)
                          : std::min(selectionAnchor, selectionCaret),
                    false);
        return;
    }
    moveCaretTo(right ? nextCodePoint(text, selectionCaret)
                      : previousCodePoint(text, selectionCaret),
                extend);
}

void SelectableTextView::moveCaretVertically(const int direction,
                                             const bool extend)
{
    if (lines.empty())
    {
        return;
    }
    const std::size_t currentLine = lineIndexForTextIndex(selectionCaret);
    const auto &line = lines[currentLine];
    const int x = textWidth(line.start, selectionCaret > line.start
                                            ? selectionCaret - line.start
                                            : 0);
    const std::size_t targetLine = static_cast<std::size_t>(
        std::clamp(static_cast<int>(currentLine) + direction, 0,
                   static_cast<int>(lines.size()) - 1));
    moveCaretTo(nearestIndexOnLine(targetLine, x), extend);
}

void SelectableTextView::moveCaretTo(const std::size_t index, const bool extend)
{
    selectionCaret = std::min(index, text.size());
    if (!extend)
    {
        selectionAnchor = selectionCaret;
    }
    ensureCaretVisible();
    setDirty();
}

void SelectableTextView::ensureCaretVisible()
{
    if (lines.empty())
    {
        return;
    }
    const double y =
        static_cast<double>(lineIndexForTextIndex(selectionCaret) * lineHeight);
    if (y < scrollOffset)
    {
        setScrollOffset(y);
    }
    else if (y + lineHeight > scrollOffset + textViewportHeight())
    {
        setScrollOffset(y + lineHeight - textViewportHeight());
    }
}

void SelectableTextView::selectWordAt(const std::size_t index)
{
    if (text.empty())
    {
        return;
    }
    std::size_t start = std::min(index, text.size());
    if (start == text.size() || !isWordCharacter(text[start]))
    {
        if (start > 0 && isWordCharacter(text[start - 1]))
        {
            --start;
        }
        else
        {
            selectionAnchor = start;
            selectionCaret = start;
            return;
        }
    }

    std::size_t end = start;
    while (start > 0 && isWordCharacter(text[start - 1]))
    {
        --start;
    }
    while (end < text.size() && isWordCharacter(text[end]))
    {
        ++end;
    }
    selectionAnchor = start;
    selectionCaret = end;
}
