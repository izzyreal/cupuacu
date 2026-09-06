#pragma once
#include <cstdint>
namespace cupuacu
{
    struct State;
}
namespace cupuacu::actions::audio
{
    void beginClipboardPaste(State *, int64_t start, int64_t end);
    void processPendingClipboardPaste(State *);
} // namespace cupuacu::actions::audio
