#pragma once
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(cupuacu);
CMRC_DECLARE(cupuacu_generated);
#include <string_view>

namespace cupuacu
{
    static std::string get_resource_data(const std::string &path)
    {
        const auto file = cmrc::cupuacu::get_filesystem().open(path.c_str());
        const auto data = std::string_view(file.begin(), file.size()).data();
        return {data, data + file.size()};
    }

    static std::string get_generated_resource_data(const std::string &path)
    {
        const auto file =
            cmrc::cupuacu_generated::get_filesystem().open(path.c_str());
        const auto data = std::string_view(file.begin(), file.size()).data();
        return {data, data + file.size()};
    }
} // namespace cupuacu
