#pragma once

#include <cmrc/cmrc.hpp>

#include <filesystem>
#include <fstream>
#include <string>

CMRC_DECLARE(cupuacu_test);

namespace cupuacu::test
{
    inline std::string get_test_resource_data(const std::string &path)
    {
        const auto file =
            cmrc::cupuacu_test::get_filesystem().open(path.c_str());
        return {file.begin(), file.end()};
    }

    inline void write_test_resource_file(const std::string &resourcePath,
                                         const std::filesystem::path &outputPath)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);

        std::ofstream out(outputPath, std::ios::binary);
        const auto data = get_test_resource_data(resourcePath);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
} // namespace cupuacu::test
