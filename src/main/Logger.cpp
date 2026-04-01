#include "Logger.hpp"

#include "Paths.hpp"

#include <SDL3/SDL.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cupuacu::logging
{
    namespace
    {
        std::shared_ptr<spdlog::logger> gLogger;
        std::mutex gLoggerMutex;
        bool gSdlBridgeInstalled = false;

        std::filesystem::path logFilePath(const Paths *paths)
        {
            if (paths == nullptr)
            {
                return std::filesystem::path("cupuacu.log");
            }
            return paths->logPath();
        }

        std::shared_ptr<spdlog::logger>
        makeLogger(const std::filesystem::path &path)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            std::vector<spdlog::sink_ptr> sinks;
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                path.string(), 1024 * 1024 * 5, 5));
#if !defined(NDEBUG)
            sinks.push_back(
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif

            auto logger = std::make_shared<spdlog::logger>(
                "cupuacu", sinks.begin(), sinks.end());
            logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] %v");
            logger->set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::warn);
            return logger;
        }

        spdlog::level::level_enum mapSdlPriorityToSpdlog(const SDL_LogPriority priority)
        {
            switch (priority)
            {
                case SDL_LOG_PRIORITY_VERBOSE:
                case SDL_LOG_PRIORITY_DEBUG:
                    return spdlog::level::debug;
                case SDL_LOG_PRIORITY_INFO:
                    return spdlog::level::info;
                case SDL_LOG_PRIORITY_WARN:
                    return spdlog::level::warn;
                case SDL_LOG_PRIORITY_ERROR:
                    return spdlog::level::err;
                case SDL_LOG_PRIORITY_CRITICAL:
                    return spdlog::level::critical;
                default:
                    return spdlog::level::info;
            }
        }

        void sdlLogCallback(void *, int category, SDL_LogPriority priority,
                            const char *message)
        {
            std::lock_guard<std::mutex> lock(gLoggerMutex);
            if (!gLogger || message == nullptr)
            {
                return;
            }

            std::string line = std::string("[SDL category ") +
                               std::to_string(category) + "] " + message;
            gLogger->log(mapSdlPriorityToSpdlog(priority), line);
        }

        void log(spdlog::level::level_enum level, std::string_view message)
        {
            std::lock_guard<std::mutex> lock(gLoggerMutex);
            if (!gLogger)
            {
                return;
            }
            gLogger->log(level, message);
        }
    } // namespace

    void initialize(const Paths *paths)
    {
        std::lock_guard<std::mutex> lock(gLoggerMutex);
        if (gLogger)
        {
            return;
        }

        gLogger = makeLogger(logFilePath(paths));
        spdlog::set_default_logger(gLogger);
        gLogger->info("Logger initialized");

        if (!gSdlBridgeInstalled)
        {
            SDL_SetLogOutputFunction(sdlLogCallback, nullptr);
            gSdlBridgeInstalled = true;
        }
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lock(gLoggerMutex);
        if (gLogger)
        {
            gLogger->info("Logger shutting down");
            gLogger->flush();
        }
        spdlog::shutdown();
        gLogger.reset();
    }

    void debug(std::string_view message)
    {
        log(spdlog::level::debug, message);
    }

    void info(std::string_view message)
    {
        log(spdlog::level::info, message);
    }

    void warn(std::string_view message)
    {
        log(spdlog::level::warn, message);
    }

    void error(std::string_view message)
    {
        log(spdlog::level::err, message);
    }
} // namespace cupuacu::logging
