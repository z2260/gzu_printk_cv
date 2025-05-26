#pragma once

#ifndef TIMESTAMP_POLICY_HPP
#define TIMESTAMP_POLICY_HPP

#include <cstdint>
#include <string>
#include <sstream>
#include <chrono>
#include <memory>

namespace timestamp {

namespace detail {

/**************************************************
* 静态断言： to_ns_impl()
***************************************************/
template <typename T, typename = void>
struct has_to_ns_impl :std::false_type {};

template <typename T>
struct has_to_ns_impl<T, std::void_t<
    decltype(std::declval<const T>().to_ns_impl())
>> : std::is_same<
        uint64_t,
        typename std::decay<decltype(std::declval<const T>().to_ns_impl())>::type
    > {};

/**************************************************
* 静态断言： now_impl()
***************************************************/
template <typename T, typename ClockType, typename = void>
struct has_now_impl :std::false_type {};

template <typename T, typename ClockType>
struct has_now_impl<T, ClockType, std::void_t<
    decltype(std::declval<const T>().now_impl())
>> : std::is_same<
        ClockType,
        typename std::decay<decltype(std::declval<const T>().now_impl())>::type
    > {};


/**************************************************
* 静态断言： to_string_impl()
***************************************************/
template <typename T, typename ClockType, typename = void>
struct has_to_string :std::false_type {};

template <typename T, typename ClockType>
struct has_to_string<T, ClockType, std::void_t<
    decltype(std::declval<T>().to_string_impl(std::declval<const ClockType&>()))
>> : std::is_same<
        std::string,
        typename std::decay<
            decltype(std::declval<T>().to_string_impl(std::declval<const ClockType&>()))
        >::type
    > {};

} // namespace detail

/**
 * @brief 时间戳基类，采用CRTP方式实现接口约束。
 *
 * @tparam Derived 派生类类型，需实现指定接口
 * @tparam ClockType 时钟类型，默认为void
 *
 * 派生类需实现以下成员函数：
 * - uint64_t to_ns_impl() const : 返回时间戳的纳秒数
 * - ClockType now_impl() const : 返回当前时间
 * - std::string to_string_impl(const ClockType&) const : 返回时间戳的字符串表示
 */
template <typename Derived, typename ClockType = void>
class TimestampBase {
public:
    /**
     * @brief 默认构造函数
     */
    TimestampBase() = default;

    /**
     * @brief 获取纳秒级时间戳
     * @return uint64_t 纳秒时间戳
     * @note 派生类需实现 to_ns_impl()
     */
    [[nodiscard]] uint64_t to_ns() const {
        static_assert(detail::has_to_ns_impl<Derived>::value,
            "Derived must implement: uint64_t to_ns_impl() const");
        return static_cast<const Derived*>(this)->to_ns_impl();
    }

    /**
     * @brief 获取当前时间点
     * @return ClockType 当前时间点
     * @note 派生类需实现 now_impl()
     */
    [[nodiscard]] ClockType now() const {
        static_assert(detail::has_now_impl<Derived, ClockType>::value,
            "Derived must implement: ClockType now_impl() const");
        return static_cast<const Derived*>(this)->now_impl();
    }

    /**
     * @brief 将时间戳转换为字符串
     * @param clock 时钟对象
     * @return std::string 字符串表示
     * @note 派生类需实现 to_string_impl(const ClockType&)
     */
    [[nodiscard]] std::string to_string(const ClockType& clock) const {
        static_assert(detail::has_to_string<Derived, ClockType>::value,
            "Derived must implement: std::string to_string_impl(const ClockType&) const");
        return static_cast<const Derived*>(this)->to_string_impl(clock);
    }

};

} // namespace timestamp

#endif // TIMESTAMP_POLICY_HPP