#pragma once

#include "utils/SmallFn.hpp"

namespace cupuacu::utils
{
    // "Simple" means "no parameters, no return type"

    using SimpleAction = SmallFn<104, void()>;
} // namespace cupuacu::utils
