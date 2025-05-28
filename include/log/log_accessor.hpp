#pragma once

#ifndef LOG_LOG_ACCESSOR_HPP
#define LOG_LOG_ACCESSOR_HPP

#include <typeinfo>
#include <cxxabi.h>
#include <string>
#include <string_view>
#include <filesystem>

#include "log/log_manager.hpp"

#if __cplusplus >= 202002L
#include <source_location>
#define HAS_SOURCE_LOCATION 1
#else
#define HAS_SOURCE_LOCATION 0
#define CURRENT_FUNCTION __func__
#define CURRENT_FILE __FILE__
#define CURRENT_LINE __LINE__
#endif

namespace logger {

enum class LogLevel {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
    critical = 5
};

template <typename T>
std::string get_type_name() {
    int status;
    char* demangled = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
    std::string result = (status == 0) ? demangled : typeid(T).name();
    std::free(demangled);

    size_t template_start = result.find('<');
    if (template_start != std::string::npos) {
        result = result.substr(0, template_start);
    }

    size_t name_start = result.rfind("::");
    if (name_start != std::string::npos) {
        result = result.substr(name_start + 2);
    }

    return result;
}

template <typename Derived, LogLevel CompileLevel = LogLevel::debug>
class LogAccessor {
public:
    static std::string class_name() {
        return get_type_name<Derived>();
    }

    static std::shared_ptr<spdlog::log_manager> get_logger() {
        return logger_;
    }

    static void configure_logger(const std::string& log_file_path,
                                size_t max_file_size = 5 * 1024 * 1024,
                                size_t max_files = 3,
                                spdlog::level::level_enum level = spdlog::level::info,
                                const std::string& log_pattern = "%Y-%m-%d %H:%M:%S [%7l]: %v") {
        std::string logger_name = class_name();
        auto& logger_manager = LogManager::getInstance();

        std::filesystem::path p(log_file_path);
        std::filesystem::create_directories(p.parent_path());

        try {
            auto logger = logger_manager.getLogger(logger_name);
            logger->set_level(level);
            logger->set_pattern(log_pattern);
        } catch (const std::runtime_error&) {
            logger_ = logger_manager.createLogger(logger_name, log_file_path,
                                                max_file_size, max_files, log_pattern);
            logger_->set_level(level);
        }
    }

    template<typename... Args>
    static void log_trace(std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::trace) {
            get_logger()->trace("[{}] " + std::string(fmt), class_name(), args...);
        }
    }

    template<typename... Args>
    static void log_debug(std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::debug) {
            get_logger()->debug("[{}] " + std::string(fmt), class_name(), args...);
        }
    }

    template<typename... Args>
    static void log_info(std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::info) {
            get_logger()->info("[{}] " + std::string(fmt), class_name(), args...);
        }
    }

    template<typename... Args>
    static void log_warn(std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::warn) {
            get_logger()->warn("[{}] " + std::string(fmt), class_name(), args...);
        }
    }

    template<typename... Args>
    static void log_error(std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::error) {
            get_logger()->error("[{}] " + std::string(fmt), class_name(), args...);
        }
    }

    template<typename... Args>
    static void log_critical(std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::critical) {
            get_logger()->critical("[{}] " + std::string(fmt), class_name(), args...);
        }
    }

#if HAS_SOURCE_LOCATION
    template<typename... Args>
    static void mlog_trace(std::string_view fmt, const Args&... args,
                         const std::source_location loc = std::source_location::current()) {
        if constexpr (CompileLevel <= LogLevel::trace) {
            get_logger()->trace("[{}::{}@{}] " + std::string(fmt), class_name(), loc.function_name(), loc.line(), args...);
        }
    }

    template<typename... Args>
    static void mlog_debug(std::string_view fmt, const Args&... args,
                         const std::source_location loc = std::source_location::current()) {
        if constexpr (CompileLevel <= LogLevel::debug) {
            get_logger()->debug("[{}::{}@{}] " + std::string(fmt), class_name(), loc.function_name(), loc.line(), args...);
        }
    }

    template<typename... Args>
    static void mlog_info(std::string_view fmt, const Args&... args,
                        const std::source_location loc = std::source_location::current()) {
        if constexpr (CompileLevel <= LogLevel::info) {
            get_logger()->info("[{}::{}@{}] " + std::string(fmt), class_name(), loc.function_name(), loc.line(), args...);
        }
    }

    template<typename... Args>
    static void mlog_warn(std::string_view fmt, const Args&... args,
                        const std::source_location loc = std::source_location::current()) {
        if constexpr (CompileLevel <= LogLevel::warn) {
            get_logger()->warn("[{}::{}@{}] " + std::string(fmt), class_name(), loc.function_name(), loc.line(), args...);
        }
    }

    template<typename... Args>
    static void mlog_error(std::string_view fmt, const Args&... args,
                         const std::source_location loc = std::source_location::current()) {
        if constexpr (CompileLevel <= LogLevel::error) {
            get_logger()->error("[{}::{}@{}] " + std::string(fmt), class_name(), loc.function_name(), loc.line(), args...);
        }
    }

    template<typename... Args>
    static void mlog_critical(std::string_view fmt, const Args&... args,
                            const std::source_location loc = std::source_location::current()) {
        if constexpr (CompileLevel <= LogLevel::critical) {
            get_logger()->critical("[{}::{}@{}] " + std::string(fmt), class_name(), loc.function_name(), loc.line(), args...);
        }
    }
#else
    template<typename... Args>
    static void mlog_trace_impl(const char* func, int line, std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::trace) {
            get_logger()->trace("[{}::{}@{}] " + std::string(fmt), class_name(), func, line, args...);
        }
    }

    template<typename... Args>
    static void mlog_debug_impl(const char* func, int line, std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::debug) {
            get_logger()->debug("[{}::{}@{}] " + std::string(fmt), class_name(), func, line, args...);
        }
    }

    template<typename... Args>
    static void mlog_info_impl(const char* func, int line, std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::info) {
            get_logger()->info("[{}::{}@{}] " + std::string(fmt), class_name(), func, line, args...);
        }
    }

    template<typename... Args>
    static void mlog_warn_impl(const char* func, int line, std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::warn) {
            get_logger()->warn("[{}::{}@{}] " + std::string(fmt), class_name(), func, line, args...);
        }
    }

    template<typename... Args>
    static void mlog_error_impl(const char* func, int line, std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::error) {
            get_logger()->error("[{}::{}@{}] " + std::string(fmt), class_name(), func, line, args...);
        }
    }

    template<typename... Args>
    static void mlog_critical_impl(const char* func, int line, std::string_view fmt, const Args&... args) {
        if constexpr (CompileLevel <= LogLevel::critical) {
            get_logger()->critical("[{}::{}@{}] " + std::string(fmt), class_name(), func, line, args...);
        }
    }

#define mlog_trace(...) mlog_trace_impl(CURRENT_FUNCTION, CURRENT_LINE, __VA_ARGS__)
#define mlog_debug(...) mlog_debug_impl(CURRENT_FUNCTION, CURRENT_LINE, __VA_ARGS__)
#define mlog_info(...) mlog_info_impl(CURRENT_FUNCTION, CURRENT_LINE, __VA_ARGS__)
#define mlog_warn(...) mlog_warn_impl(CURRENT_FUNCTION, CURRENT_LINE, __VA_ARGS__)
#define mlog_error(...) mlog_error_impl(CURRENT_FUNCTION, CURRENT_LINE, __VA_ARGS__)
#define mlog_critical(...) mlog_critical_impl(CURRENT_FUNCTION, CURRENT_LINE, __VA_ARGS__)
#endif // HAS_SOURCE_LOCATION

protected:
    LogAccessor() = default;
    ~LogAccessor() = default;

private:
    static std::shared_ptr<spdlog::log_manager> init_logger() {
        static std::string logger_name = get_type_name<Derived>();
        try {
            return LogManager::getInstance().getLogger(logger_name);
        } catch (const std::runtime_error&) {
            std::filesystem::create_directories("logs");
            std::string log_path = "logs/" + logger_name + ".log";
            return LogManager::getInstance().createLogger(logger_name, log_path);
        }
    }

    static inline std::shared_ptr<spdlog::log_manager> logger_ = init_logger();
};

#define TRACE(...) this->Derived::log_trace(__VA_ARGS__)
#define DEBUG(...) this->Derived::log_debug(__VA_ARGS__)
#define INFO(...) this->Derived::log_info(__VA_ARGS__)
#define WARN(...) this->Derived::log_warn(__VA_ARGS__)
#define ERROR(...) this->Derived::log_error(__VA_ARGS__)
#define CRITICAL(...) this->Derived::log_critical(__VA_ARGS__)

#if HAS_SOURCE_LOCATION
#define MTRACE(...) Derived::mlog_trace(__VA_ARGS__)
#define MDEBUG(...) Derived::mlog_debug(__VA_ARGS__)
#define MINFO(...) Derived::mlog_info(__VA_ARGS__)
#define MWARN(...) Derived::mlog_warn(__VA_ARGS__)
#define MERROR(...) Derived::mlog_error(__VA_ARGS__)
#define MCRITICAL(...) Derived::mlog_critical(__VA_ARGS__)
#else
#define MTRACE(...) this->mlog_trace(__VA_ARGS__)
#define MDEBUG(...) this->mlog_debug(__VA_ARGS__)
#define MINFO(...) this->mlog_info(__VA_ARGS__)
#define MWARN(...) this->mlog_warn(__VA_ARGS__)
#define MERROR(...) this->mlog_error(__VA_ARGS__)
#define MCRITICAL(...) this->mlog_critical(__VA_ARGS__)
#endif

} // namespace logger

#endif // LOG_LOG_ACCESSOR_HPP