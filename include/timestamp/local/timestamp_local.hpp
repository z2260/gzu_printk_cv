#pragma once

#ifndef TIMESTAMP_LOCAL_HPP
#define TIMESTAMP_LOCAL_HPP

#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>

namespace timestamp {

template<typename Clock>
struct ClockPolicy {
    using time_point = typename Clock::time_point;

    static time_point now() noexcept {
        return Clock::now();
    }

    static int64_t to_ns(const time_point& tp) noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch()).count();
    }

    static std::string to_string(const time_point& tp) {
        if constexpr (std::is_same_v<Clock, std::chrono::system_clock>) {
            return format_system_time(tp);
        } else {
            auto ns = to_ns(tp);
            std::ostringstream oss;
            oss << ns << " ns";
            return oss.str();
        }
    }

private:
    static std::string format_system_time(const time_point& tp) {
        auto time_t_value = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf;

    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t_value);
    #else
        localtime_r(&time_t_value, &tm_buf);
    #endif
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch() % std::chrono::seconds(1)).count();

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms;
        return oss.str();
    }
};

/**************************************************
* System Timestamp (From Std Library)
***************************************************/
class SystemTimestamp {
public:
    using time_point_t = std::chrono::system_clock::time_point;
    using policy_t = ClockPolicy<std::chrono::system_clock>;

    time_point_t now() const noexcept {
        return policy_t::now();
    }

    int64_t to_ns(const time_point_t& tp) const noexcept {
        return policy_t::to_ns(tp);
    }

    std::string to_string(const time_point_t& tp) const {
        return policy_t::to_string(tp);
    }
};

/**************************************************
* Steady Timestamp (From Std Library, It's More Precise)
***************************************************/
class SteadyTimestamp {
public:
    using time_point_t = std::chrono::steady_clock::time_point;
    using policy_t = ClockPolicy<std::chrono::steady_clock>;

    time_point_t now() const noexcept {
        return policy_t::now();
    }

    int64_t to_ns(const time_point_t& tp) const noexcept {
        return policy_t::to_ns(tp);
    }

    std::string to_string(const time_point_t& tp) const {
        return policy_t::to_string(tp);
    }
};

// 便利的类型别名
using SystemClock = ClockPolicy<std::chrono::system_clock>;
using SteadyClock = ClockPolicy<std::chrono::steady_clock>;

} // namespace timestamp

#endif // TIMESTAMP_LOCAL_HPP