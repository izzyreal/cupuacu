#pragma once

#include <sndfile.h>

#include <filesystem>
#include <string>

namespace cupuacu::file
{
    inline SNDFILE *openSndfile(const std::filesystem::path &path, const int mode,
                                SF_INFO *info)
    {
#if defined(_WIN32)
        const std::wstring widePath = path.wstring();
        return sf_wchar_open(widePath.c_str(), mode, info);
#else
        const std::string narrowPath = path.string();
        return sf_open(narrowPath.c_str(), mode, info);
#endif
    }
} // namespace cupuacu::file
