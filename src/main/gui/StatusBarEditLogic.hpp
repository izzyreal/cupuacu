#pragma once

#include "../DocumentSession.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace cupuacu::gui
{
    inline std::optional<int64_t> parseStatusBarFrameIndex(
        const std::string &text)
    {
        if (text.empty())
        {
            return std::nullopt;
        }

        try
        {
            size_t consumed = 0;
            const auto value = std::stoll(text, &consumed);
            if (consumed != text.size())
            {
                return std::nullopt;
            }
            return value;
        }
        catch (const std::exception &)
        {
            return std::nullopt;
        }
    }

    inline void applyStatusBarLengthFromStart(DocumentSession &session,
                                              const int64_t startFrame,
                                              const int64_t desiredLength)
    {
        const int64_t frameCount = session.document.getFrameCount();
        const int64_t clampedStart =
            std::clamp(startFrame, int64_t{0}, frameCount);
        const int64_t clampedLength = std::max<int64_t>(0, desiredLength);
        const int64_t endExclusive =
            std::clamp(clampedStart + clampedLength, clampedStart, frameCount);

        if (endExclusive == clampedStart)
        {
            session.selection.reset();
        }
        else
        {
            session.selection.setValue1(clampedStart);
            session.selection.setValue2(endExclusive);
        }
    }

    inline bool tryApplyStatusBarPositionEdit(DocumentSession &session,
                                              const std::string &text)
    {
        const auto value = parseStatusBarFrameIndex(text);
        if (!value.has_value())
        {
            return false;
        }

        session.cursor = std::clamp(*value, int64_t{0},
                                    session.document.getFrameCount());
        return true;
    }

    inline bool tryApplyStatusBarStartEdit(DocumentSession &session,
                                           const std::string &text)
    {
        const auto value = parseStatusBarFrameIndex(text);
        if (!value.has_value())
        {
            return false;
        }

        if (!session.selection.isActive())
        {
            session.cursor = std::clamp(*value, int64_t{0},
                                        session.document.getFrameCount());
            return true;
        }

        const int64_t selectionLength = session.selection.getLengthInt();
        const int64_t frameCount = session.document.getFrameCount();
        const int64_t startFrame =
            std::clamp(*value, int64_t{0},
                       std::max<int64_t>(0, frameCount - selectionLength));
        applyStatusBarLengthFromStart(session, startFrame, selectionLength);
        return true;
    }

    inline bool tryApplyStatusBarEndEdit(DocumentSession &session,
                                         const std::string &text)
    {
        const auto value = parseStatusBarFrameIndex(text);
        if (!value.has_value())
        {
            return false;
        }

        const int64_t frameCount = session.document.getFrameCount();

        if (!session.selection.isActive())
        {
            const int64_t cursor = std::clamp(
                session.cursor, int64_t{0}, std::max<int64_t>(0, frameCount));
            const int64_t inclusiveEnd = std::clamp(
                *value, int64_t{0}, std::max<int64_t>(0, frameCount - 1));
            const int64_t selectionStart = std::min(cursor, inclusiveEnd);
            const int64_t selectionEndExclusive =
                std::max(cursor, inclusiveEnd) + 1;
            applyStatusBarLengthFromStart(
                session, selectionStart, selectionEndExclusive - selectionStart);
            return true;
        }

        const int64_t startFrame = session.selection.getStartInt();
        const int64_t inclusiveEnd = std::clamp(
            *value, startFrame - 1, std::max<int64_t>(-1, frameCount - 1));
        applyStatusBarLengthFromStart(session, startFrame,
                                      inclusiveEnd - startFrame + 1);
        return true;
    }

    inline bool tryApplyStatusBarLengthEdit(DocumentSession &session,
                                            const std::string &text)
    {
        const auto value = parseStatusBarFrameIndex(text);
        if (!value.has_value())
        {
            return false;
        }

        const int64_t startFrame =
            session.selection.isActive() ? session.selection.getStartInt()
                                         : session.cursor;
        applyStatusBarLengthFromStart(session, startFrame, *value);
        return true;
    }
} // namespace cupuacu::gui
