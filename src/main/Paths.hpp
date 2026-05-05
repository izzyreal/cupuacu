#pragma once

#include <memory>
#include <filesystem>

namespace cupuacu
{
    class Paths
    {
    protected:
        virtual std::filesystem::path appConfigHome() const;
        virtual std::filesystem::path appLogHome() const;
        virtual std::filesystem::path appDocumentsPath() const;

    public:
        virtual ~Paths() = default;
        struct Documents
        {
            explicit Documents(const Paths *paths);

            std::filesystem::path appDocumentsPath() const;

            std::filesystem::path logFilePath() const;

            std::filesystem::path tempPath() const;

        private:
            const Paths *paths;
        };

        Paths();

        std::filesystem::path configPath() const;

        std::filesystem::path keyboardBindingsPath() const;

        std::filesystem::path audioDevicePropertiesPath() const;

        std::filesystem::path displayPropertiesPath() const;

        std::filesystem::path recentlyOpenedFilesPath() const;

        std::filesystem::path sessionStatePath() const;

        std::filesystem::path statePath() const;

        std::filesystem::path autosavePath() const;

        std::filesystem::path undoPath() const;

        std::filesystem::path logPath() const;

        Documents *getDocuments() const;

    private:
        const std::unique_ptr<Documents> documents;
    };
} // namespace cupuacu
