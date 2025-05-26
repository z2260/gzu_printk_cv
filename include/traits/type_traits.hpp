#pragma once

#ifndef TRAITS_TYPE_TRAITS_UTILS_HPP
#define TRAITS_TYPE_TRAITS_UTILS_HPP

#include <type_traits>
#include <utility>
#include <iostream>

namespace traits {

template <typename R, typename F, typename... Args>
struct is_callable_r : std::is_invocable_r<R, F, Args...> {};

template <typename R, typename F, typename... Args>
constexpr bool is_callable_r_v = is_callable_r<R, F, Args...>::value;

} // namespace traits

#endif // TRAITS_TYPE_TRAITS_UTILS_HPP