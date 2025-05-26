#pragma once

#ifndef SENSOR_CAMERA_BASE_HPP
#define SENSOR_CAMERA_BASE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sensor/sensor_base.hpp"
#include "sensor/camera/camera_accessor.hpp"
#include "sensor/camera/camera_utils.hpp"

namespace sensor::camera {

template <typename DataType = std::vector<uint8_t>>
struct Image_t {
    DataType raw_data;
};

template <typename Derived, typename DataType = std::vector<uint8_t>>
class CameraBase
    :   public SensorBase<Derived, DataType>
{
public:
    bool start_capture() {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)()>(&Derived::start_capture_impl)),
                          Derived&>,
                      "Derived must implement: bool start_capture_impl()");
        return static_cast<Derived*>(this)->start_capture_impl();
    }

    bool is_captured() {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)()>(&Derived::is_captured_impl)),
                          Derived&>,
                      "Derived must implement: bool is_captured_impl()");
        return static_cast<Derived*>(this)->is_captured_impl();
    }

    bool stop_capture() {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)()>(&Derived::stop_capture_impl)),
                          Derived&>,
                      "Derived must implement: bool stop_capture_impl()");
        return static_cast<Derived*>(this)->stop_capture_impl();
    }

    bool get_frame(DataType& frame) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(DataType&)>(&Derived::get_frame_impl)),
                          Derived&, DataType&>,
                      "Derived must implement: bool get_frame_impl(DataType&)");
        return static_cast<Derived*>(this)->get_frame_impl(frame);
    }

    std::shared_ptr<DataType> get_frame() {
        static_assert(traits::is_callable_r_v<
                          std::shared_ptr<DataType>,
                          decltype(static_cast<std::shared_ptr<DataType> (Derived::*)()>(&Derived::get_frame_impl)),
                          Derived&>,
                      "Derived must implement: std::shared_ptr<DataType> get_frame_impl()");
        return static_cast<Derived*>(this)->get_frame_impl();
    }

    bool set_rotate(uint8_t rotation) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(uint8_t)>(&Derived::set_rotate_impl)),
                          Derived&, uint8_t>,
                      "Derived must implement: bool set_rotate_impl(uint8_t)");
        return static_cast<Derived*>(this)->set_rotate_impl(rotation);

    }
};

} // namespace sensor::camera

#endif // SENSOR_CAMERA_BASE_HPP