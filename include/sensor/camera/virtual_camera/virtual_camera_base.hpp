#pragma once

#ifndef CORE_SENSOR_POLICY_HPP
#define CORE_SENSOR_POLICY_HPP

#include <string>
#include <opencv2/opencv.hpp>
#include <utility> // for std::pair

namespace sensor::camera {

enum class VideoSourceType {
    IMAGE,
    VIDEO
};

template<VideoSourceType Type>
struct VideoSourceTypeTraits;

template<>
struct VideoSourceTypeTraits<VideoSourceType::IMAGE> {
    using source_type = std::string;
    using data_type = cv::Mat;

    static bool open(const source_type& source, data_type& data) {
        data = cv::imread(source);
        return !data.empty();
    }

    static bool get_frame(data_type& data, data_type& frame) {
        data.copyTo(frame);
        return !frame.empty();
    }

    static void close(data_type& data) {
        data.release();
    }

    static int get_fps(const data_type& data) {
        return 30;
    }

    static std::pair<int, int> get_resolution(const data_type& data) {
        // Return the image dimensions
        return {data.cols, data.rows};
    }
};

template<>
struct VideoSourceTypeTraits<VideoSourceType::VIDEO> {
    using source_type = std::string;
    using data_type = cv::VideoCapture;

    static bool open(const source_type& source, data_type& data) {
        try {
            int device_id = std::stoi(source);
            return data.open(device_id);
        } catch (const std::invalid_argument&) {
            return data.open(source);
        }
    }

    static bool get_frame(data_type& data, cv::Mat& frame) {
        return data.read(frame);
    }

    static void close(data_type& data) {
        data.release();
    }

    static int get_fps(const data_type& data) {
        return static_cast<int>(data.get(cv::CAP_PROP_FPS));
    }

    static std::pair<int, int> get_resolution(const data_type& data) {
        int width = static_cast<int>(data.get(cv::CAP_PROP_FRAME_WIDTH));
        int height = static_cast<int>(data.get(cv::CAP_PROP_FRAME_HEIGHT));
        return {width, height};
    }
};

} // namespace sensor::camera

#endif // CORE_SENSOR_POLICY_HPP