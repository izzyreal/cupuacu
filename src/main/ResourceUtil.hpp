#pragma once
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(cupuacu);
#include <string_view>

static std::string get_resource_data(const std::string &path)
{
    const auto file = cmrc::cupuacu::get_filesystem().open(path.c_str());
    const auto data = std::string_view(file.begin(), file.size()).data();
    return { data, data + file.size() };
}

