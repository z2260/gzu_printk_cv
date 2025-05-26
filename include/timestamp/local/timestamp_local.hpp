#pragma once

#ifndef TIMESTAMP_LOCAL_HPP
#define TIMESTAMP_LOCAL_HPP

#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>

namespace timestamp {

/**************************************************
* System Timestamp (From Std Library)
***************************************************/
class SystemTimestamp {
public:
    using time_point_t = std::chrono::system_clock::time_point;

    time_point_t now() const {
        return std::chrono::system_clock::now();
    }

    int64_t to_ns(const time_point_t& tp) const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch()).count();
    }

    std::string to_string(const time_point_t& tp) const {
        auto time_t_value = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf;

    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t_value);
    #else
        localtime_r(&time_t_value, &tm_buf);
    #endif
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch() % std::chrono::seconds(1)).count();

        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);

        std::ostringstream oss;
        oss << buffer << '.' << std::setfill('0') << std::setw(3) << ms;
        return oss.str();
    }
};

/**************************************************
* Steady Timestamp (From Std Library, It's More Precise)
***************************************************/
class SteadyTimestamp {
public:
    using time_point_t = std::chrono::steady_clock::time_point;

    time_point_t now() const {
        return std::chrono::steady_clock::now();
    }

    int64_t to_ns(const time_point_t& tp) const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch()).count();
    }

    std::string to_string(const time_point_t& tp) const {
        auto ns = to_ns(tp);
        std::ostringstream oss;
        oss << ns << " ns";
        return oss.str();
    }
};

} // namespace timestamp

#endif // TIMESTAMP_LOCAL_HPP