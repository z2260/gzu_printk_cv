#pragma once

#ifndef COMM_CORE_TRAITS_HPP
#define COMM_CORE_TRAITS_HPP

#include <type_traits>
#include <utility>
#include <optional>
#include <cstdint>
#include <vector>
#include <array>
#include <functional>

namespace comm::traits {

template <typename R, typename F, typename... Args>
struct is_callable_r : std::is_invocable_r<R, F, Args...> {};

template <typename R, typename F, typename... Args>
constexpr bool is_callable_r_v = is_callable_r<R, F, Args...>::value;

template<typename T>
class buffer_view {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;

    constexpr buffer_view() noexcept : data_(nullptr), size_(0) {}
    constexpr buffer_view(pointer data, size_type size) noexcept : data_(data), size_(size) {}

    template<std::size_t N>
    constexpr buffer_view(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

    template<std::size_t N>
    constexpr buffer_view(std::array<T, N>& arr) noexcept : data_(arr.data()), size_(N) {}

    constexpr buffer_view(std::vector<T>& vec) noexcept : data_(vec.data()), size_(vec.size()) {}

    constexpr pointer data() const noexcept { return data_; }
    constexpr size_type size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr reference operator[](size_type idx) const { return data_[idx]; }
    constexpr pointer begin() const noexcept { return data_; }
    constexpr pointer end() const noexcept { return data_ + size_; }

private:
    pointer data_;
    size_type size_;
};

#define COMM_HAS_MEMBER(member_name) \
    template<typename T, typename = void> \
    struct has_##member_name : std::false_type {}; \
    template<typename T> \
    struct has_##member_name<T, std::void_t<decltype(std::declval<T>().member_name)>> : std::true_type {}; \
    template<typename T> \
    constexpr bool has_##member_name##_v = has_##member_name<T>::value;

#define COMM_HAS_METHOD(method_name, ...) \
    template<typename T, typename = void> \
    struct has_##method_name : std::false_type {}; \
    template<typename T> \
    struct has_##method_name<T, std::void_t<decltype(std::declval<T>().method_name(__VA_ARGS__))>> : std::true_type {}; \
    template<typename T> \
    constexpr bool has_##method_name##_v = has_##method_name<T>::value;

COMM_HAS_METHOD(write, std::declval<std::uint32_t>(), std::declval<buffer_view<const std::uint8_t>>())
COMM_HAS_METHOD(read)
COMM_HAS_METHOD(mtu)

COMM_HAS_METHOD(is_connected)
COMM_HAS_METHOD(close)
COMM_HAS_METHOD(get_stats)

template<typename T>
struct is_link_policy {
    static constexpr bool value =
        has_write_v<T> &&
        has_read_v<T> &&
        has_mtu_v<T>;
};

template<typename T>
constexpr bool is_link_policy_v = is_link_policy<T>::value;

COMM_HAS_METHOD(wrap, std::declval<buffer_view<const std::uint8_t>>())
COMM_HAS_METHOD(unwrap, std::declval<buffer_view<const std::uint8_t>>())

template<typename T>
struct is_transport_policy {
    static constexpr bool value =
        has_wrap_v<T> &&
        has_unwrap_v<T>;
};

template<typename T>
constexpr bool is_transport_policy_v = is_transport_policy<T>::value;

template<typename T, typename = void>
struct has_encode : std::false_type {};

template<typename T>
struct has_encode<T, std::void_t<
    decltype(std::declval<T>().template encode<int>(std::declval<const int&>()))
>> : std::true_type {};

template<typename T>
constexpr bool has_encode_v = has_encode<T>::value;

COMM_HAS_METHOD(dispatch, std::declval<buffer_view<const std::uint8_t>>(), std::declval<std::function<void(int)>>())

template<typename T>
struct is_message_policy {
    static constexpr bool value =
        has_encode_v<T> &&
        has_dispatch_v<T>;
};

template<typename T>
constexpr bool is_message_policy_v = is_message_policy<T>::value;

template<typename T, typename Handler, typename = void>
struct has_handle : std::false_type {};

template<typename T, typename Handler>
struct has_handle<T, Handler, std::void_t<
    decltype(std::declval<T>().template handle<int>(std::declval<int>(), std::declval<Handler>()))
>> : std::true_type {};

template<typename T, typename Handler = std::function<void()>>
constexpr bool has_handle_v = has_handle<T, Handler>::value;

template<typename T>
struct is_service_policy {
    template<typename Handler>
    static constexpr bool check() {
        return has_handle_v<T, Handler>;
    }

    static constexpr bool value = has_handle_v<T>;
};

template<typename T>
constexpr bool is_service_policy_v = is_service_policy<T>::value;

template<typename T>
struct supports_zero_copy : std::false_type {};

template<typename T>
constexpr bool supports_zero_copy_v = supports_zero_copy<T>::value;

template<typename T>
struct supports_compression : std::false_type {};

template<typename T>
constexpr bool supports_compression_v = supports_compression<T>::value;

template<typename T>
struct supports_encryption : std::false_type {};

template<typename T>
constexpr bool supports_encryption_v = supports_encryption<T>::value;

template<typename T>
struct is_realtime_capable : std::false_type {};

template<typename T>
constexpr bool is_realtime_capable_v = is_realtime_capable<T>::value;

template<typename T>
struct memory_model {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = false;
};

template<typename T, typename = void>
struct buffer_traits {
    using value_type = std::uint8_t;
    static constexpr bool is_contiguous = false;
    static constexpr bool is_resizable = false;
};

template<typename T>
struct buffer_traits<buffer_view<T>> {
    using value_type = T;
    static constexpr bool is_contiguous = true;
    static constexpr bool is_resizable = false;
};

template<typename T>
struct buffer_traits<std::vector<T>> {
    using value_type = T;
    static constexpr bool is_contiguous = true;
    static constexpr bool is_resizable = true;
};

template<typename T, std::size_t N>
struct buffer_traits<std::array<T, N>> {
    using value_type = T;
    static constexpr bool is_contiguous = true;
    static constexpr bool is_resizable = false;
};

template<typename T>
struct is_endpoint_id : std::false_type {};

template<>
struct is_endpoint_id<std::uint32_t> : std::true_type {};

template<>
struct is_endpoint_id<std::uint64_t> : std::true_type {};

template<typename T>
constexpr bool is_endpoint_id_v = is_endpoint_id<T>::value;

template<typename LinkPolicy, typename TransportPolicy,
         typename MessagePolicy, typename ServicePolicy>
struct is_valid_pipeline_combination {
    static constexpr bool value =
        is_link_policy_v<LinkPolicy> &&
        is_transport_policy_v<TransportPolicy> &&
        is_message_policy_v<MessagePolicy> &&
        is_service_policy_v<ServicePolicy>;
};

template<typename LinkPolicy, typename TransportPolicy,
         typename MessagePolicy, typename ServicePolicy>
constexpr bool is_valid_pipeline_combination_v =
    is_valid_pipeline_combination<LinkPolicy, TransportPolicy, MessagePolicy, ServicePolicy>::value;

template<typename T, typename Signature>
struct has_function_signature : std::false_type {};

template<typename T, typename R, typename... Args>
struct has_function_signature<T, R(Args...)> {
    template<typename U>
    static auto test(int) -> decltype(
        std::declval<U>()(std::declval<Args>()...),
        std::is_same_v<R, decltype(std::declval<U>()(std::declval<Args>()...))>
    );

    template<typename>
    static std::false_type test(...);

    static constexpr bool value = decltype(test<T>(0))::value;
};

template<typename T, typename Signature>
constexpr bool has_function_signature_v = has_function_signature<T, Signature>::value;

template<typename T>
struct dependent_false : std::false_type {};

template<typename T>
constexpr bool dependent_false_v = dependent_false<T>::value;

} // namespace comm::traits

#endif // COMM_CORE_TRAITS_HPP