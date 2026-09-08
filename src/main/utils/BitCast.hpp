#pragma once

#include <cstring>
#include <type_traits>

namespace cupuacu::utils
{
    // GCC 10's standard library lacks std::bit_cast. Fixed-size memcpy keeps
    // the same object representation without aliasing or numeric conversion.
    template <class To, class From> To bitCast(const From &value) noexcept
    {
        static_assert(sizeof(To) == sizeof(From));
        static_assert(std::is_trivially_copyable_v<To>);
        static_assert(std::is_trivially_copyable_v<From>);
        To result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }
} // namespace cupuacu::utils
