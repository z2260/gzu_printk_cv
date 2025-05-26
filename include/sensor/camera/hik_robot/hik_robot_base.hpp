#pragma once

#ifndef SENSOR_CAMERA_HIK_ROBOT_BASE_HPP
#define SENSOR_CAMERA_HIK_ROBOT_BASE_HPP

namespace sensor::camera {

enum class HikRobotModel {
    MV_CS016_10UC,
    MV_CU013_A0UC,
    // Todo: 添加更多的相机型号
};

template <HikRobotModel Model>
struct HikRobotModelTraits {
    static constexpr int MaxWidth = 0;
    static constexpr int MaxHeight = 0;
    // Todo: 添加更多的特性
};

template <>
struct HikRobotModelTraits<HikRobotModel::MV_CS016_10UC> {
    static constexpr int MaxWidth = 1440;
    static constexpr int MaxHeight = 1080;
};

template <>
struct HikRobotModelTraits<HikRobotModel::MV_CU013_A0UC> {
    static constexpr int MaxWidth = 1280;
    static constexpr int MaxHeight = 1024;
};


} // namespace sensor::camera

#endif // SENSOR_CAMERA_HIK_ROBOT_BASE_HPP