#pragma once
#include <memory>

namespace cupuacu
{
    struct State;
    namespace gui
    {
        class Window;
    }
} // namespace cupuacu
namespace cupuacu::effects
{
    class PitchDialog
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

    public:
        explicit PitchDialog(State *);
        ~PitchDialog();
        bool isOpen() const;
        void raise() const;
        gui::Window *getWindow() const;
    };
} // namespace cupuacu::effects
