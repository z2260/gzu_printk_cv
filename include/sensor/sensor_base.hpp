#pragma once

#ifndef SENSOR_BASE_HPP
#define SENSOR_BASE_HPP

#include "traits/type_traits.hpp"

namespace sensor {

/**
 * @brief 通用传感器基类，提供统一的初始化、打开、关闭、数据获取等接口。
 *
 * @tparam Derived 派生类类型，需实现相关接口方法。
 * @tparam DataType 传感器数据类型。
 *
 * 派生类需要实现以下接口方法：
 * - bool init_impl()：初始化传感器。
 * - bool open_impl()：打开传感器。
 * - bool close_impl()：关闭传感器。
 * - bool is_open_impl()：判断传感器是否已打开。
 * - void get_data_impl(DataType& data)：通过引用获取数据。
 * - DataType get_data_impl()：通过返回值获取数据。
 *
 */
template <typename Derived, typename DataType>
class SensorBase {
public:
    SensorBase() = default;

    bool init() {
        static_assert(traits::is_callable_r_v<bool,
                                          decltype(&Derived::init_impl),
                                          Derived&>,
                  "Derived must implement: bool init_impl()");
        return static_cast<Derived*>(this)->init_impl();
    }

    bool open() {
        static_assert(traits::is_callable_r_v<bool,
            bool (Derived::*)(),
            Derived&>,
            "Derived must implement: bool open_impl()");
        return static_cast<Derived*>(this)->open_impl();
    }

    bool open(int index) {
        static_assert(traits::is_callable_r_v<bool,
            bool (Derived::*)(int),
            Derived&, int>,
            "Derived must implement: bool open_impl(int)");
        return static_cast<Derived*>(this)->open_impl(index);
    }

    bool close() {
        static_assert(traits::is_callable_r_v<bool,
                                              decltype(&Derived::close_impl),
                                              Derived&>,
                      "Derived must implement: bool close_impl()");
        return static_cast<Derived*>(this)->close_impl();
    }

    bool is_open() {
        static_assert(traits::is_callable_r_v<bool,
                                              decltype(&Derived::is_open_impl),
                                              Derived&>,
                      "Derived must implement: bool is_open_impl()");
        return static_cast<Derived*>(this)->is_open_impl();
    }

    bool get_data(DataType& data) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(DataType&)>(&Derived::get_data_impl)),
                          Derived&, DataType&>,
                      "Derived must implement: bool get_data_impl(DataType&)");
        return static_cast<Derived*>(this)->get_data_impl(data);
    }

    DataType get_data() {
        static_assert(traits::is_callable_r_v<
                          DataType,
                          decltype(static_cast<DataType (Derived::*)()>(&Derived::get_data_impl)),
                          Derived&>,
                      "Derived must implement: DataType get_data_impl()");
        return static_cast<Derived*>(this)->get_data_impl();
    }

};



} // namespace sensor

#endif // SENSOR_BASE_HPP