#pragma once

#ifndef LOG_LOG_MANAGER_HPP
#define LOG_LOG_MANAGER_HPP

#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <chrono>
#include <iostream>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/log_manager.h>
#include <spdlog/details/thread_pool.h>

namespace logger {

class LogManager {
public:
    /**
     * @brief 获取 LogManager 单例实例。
     * @return LogManager 的单例实例。
     */
    static LogManager& getInstance() {
        static LogManager instance;
        return instance;
    }
    
    /**
     * @brief 注册日志服务。
     * @param logger_name 日志器的名称。
     * @param logger 日志器实例。
     * @throw std::runtime_error 如果日志器已存在。
     */
    void registerLogger(const std::string& logger_name, const std::shared_ptr<spdlog::log_manager>& logger) {
        if (loggers_.count(logger_name) > 0) {
            throw std::runtime_error("Logger already registered: " + logger_name);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        loggers_[logger_name] = logger;
    }

    /**
     * @brief 获取已注册的日志服务。
     * @param logger_name 日志器的名称。
     * @return 已注册的日志器实例。
     * @throw std::runtime_error 如果日志器不存在。
     */
    std::shared_ptr<spdlog::log_manager> getLogger(const std::string& logger_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = loggers_.find(logger_name);
        if (it != loggers_.end()) {
            return it->second;
        }
        throw std::runtime_error("Logger not found: " + logger_name);
    }

    /**
     * @brief 设置日志级别。
     * @param logger_name 日志器的名称。
     * @param level 日志级别。
     */
    void setLogLevel(const std::string& logger_name, spdlog::level::level_enum level) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto logger = getLogger(logger_name);
        if (logger) {
            logger->set_level(level);
        }
    }

    /**
     * @brief 创建并注册一个新的日志器（支持文件和终端输出）。
     * @param logger_name 日志器的名称。
     * @param log_file_path 日志文件路径。
     * @param max_file_size 最大文件大小（默认 5MB）。
     * @param max_files 最大文件数量（默认保留 3 个文件）。
     * @param log_pattern 日志格式（默认格式为 "%Y-%m-%d %H:%M:%S %l: %v"）。
     * @return 创建的日志器实例。
     * @throw std::runtime_error 如果日志器已存在。
     */
    std::shared_ptr<spdlog::log_manager> createLogger(const std::string& logger_name,
                                                 const std::string& log_file_path,
                                                 size_t max_file_size = 5 * 1024 * 1024,
                                                 size_t max_files = 3,
                                                 const std::string& log_pattern = "%Y-%m-%d %H:%M:%S [%l]: [%10n] %v") {
        if (loggers_.count(logger_name) > 0) {
            throw std::runtime_error("Logger already exists: " + logger_name);
        }

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file_path, max_file_size, max_files);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        std::vector<spdlog::sink_ptr> sinks{file_sink, console_sink};

        auto logger = std::make_shared<spdlog::async_logger>(
            logger_name,
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );

        logger->set_pattern(log_pattern);

        logger->set_level(spdlog::level::info);

        std::lock_guard<std::mutex> lock(mutex_);
        loggers_[logger_name] = logger;

        return logger;
    }

    /**
     * @brief 清理过期日志文件。
     * @param log_directory 日志文件目录。
     * @param days_to_keep 保留的天数。
     */
    static void cleanupOldLogs(const std::string& log_directory, int days_to_keep) {
        namespace fs = std::filesystem;
        auto now = std::chrono::system_clock::now();
        for (const auto& entry : fs::directory_iterator(log_directory)) {
            if (entry.is_regular_file()) {
                auto last_write_time = fs::last_write_time(entry.path());
                auto file_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(last_write_time).time_since_epoch();
                auto now_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(now).time_since_epoch();
                if (std::chrono::duration_cast<std::chrono::hours>(now_time - file_time).count() > days_to_keep * 24) {
                    try {
                        fs::remove(entry.path());
                    } catch (const std::exception& e) {
                        spdlog::error("Failed to remove log file {}: {}", entry.path().string(), e.what());
                    }
                }
            }
        }
    }

private:
    LogManager() {
        std::cout << "Initializing global thread pool..." << std::endl;
        spdlog::init_thread_pool(8192, 2);
    }

    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    std::unordered_map<std::string, std::shared_ptr<spdlog::log_manager>> loggers_; ///< 存储所有已注册的日志服务

    std::mutex mutex_;
};

} // namespace logger

#endif // LOG_LOG_MANAGER_HPP