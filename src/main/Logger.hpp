#pragma once

#include <string_view>

namespace cupuacu
{
    class Paths;

    namespace logging
    {
        void initialize(const Paths *paths);
        void shutdown();

        void debug(std::string_view message);
        void info(std::string_view message);
        void warn(std::string_view message);
        void error(std::string_view message);
    } // namespace logging
} // namespace cupuacu
