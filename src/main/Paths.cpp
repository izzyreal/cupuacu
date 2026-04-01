#include "Paths.hpp"

#include <sago/platform_folders.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if TARGET_OS_IOS
std::string getIosSharedDocumentsFolder();
#endif

using namespace cupuacu;

Paths::Documents::Documents(const Paths *paths) : paths(paths) {}

std::filesystem::path Paths::Documents::appDocumentsPath() const
{
    return paths->appDocumentsPath();
}

Paths::Paths() : documents(std::make_unique<Documents>(this)) {}

std::filesystem::path Paths::appDocumentsPath() const
{
#if TARGET_OS_IOS
    auto path = std::filesystem::path(getIosSharedDocumentsFolder());
#else
    auto path = std::filesystem::path(sago::getDocumentsFolder()) / "Cupuacu";
#endif
    return path;
}

std::filesystem::path Paths::appConfigHome() const
{
    auto path = std::filesystem::path(sago::getConfigHome()) / "Cupuacu";
    return path;
}

std::filesystem::path Paths::appLogHome() const
{
#if defined(__APPLE__)
    return std::filesystem::path(sago::internal::getHome()) / "Library" / "Logs" /
           "Cupuacu";
#else
    return std::filesystem::path(sago::getStateDir()) / "Cupuacu" / "Logs";
#endif
}

std::filesystem::path Paths::configPath() const
{
    auto path = appConfigHome() / "config";
    return path;
}

std::filesystem::path Paths::keyboardBindingsPath() const
{
    auto path = configPath() / "keyboard_bindings.json";
    return path;
}

std::filesystem::path Paths::audioDevicePropertiesPath() const
{
    auto path = configPath() / "audio_device_properties.json";
    return path;
}

std::filesystem::path Paths::recentlyOpenedFilesPath() const
{
    auto path = configPath() / "recently_opened_files.json";
    return path;
}

std::filesystem::path Paths::sessionStatePath() const
{
    auto path = configPath() / "session_state.json";
    return path;
}

std::filesystem::path Paths::logPath() const
{
    return appLogHome() / "cupuacu.log";
}

Paths::Documents *Paths::getDocuments() const
{
    return documents.get();
}

std::filesystem::path Paths::Documents::logFilePath() const
{
    return paths->logPath();
}

std::filesystem::path Paths::Documents::tempPath() const
{
    auto tempPath = appDocumentsPath() / "Temp";
    return tempPath;
}
