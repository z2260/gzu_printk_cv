#pragma once

#ifndef SENSOR_CAMERA_UTILS_HPP
#define SENSOR_CAMERA_UTILS_HPP

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "traits/type_traits.hpp"

namespace sensor::camera {

template <typename DataType>
class CameraUtilsRotateAccessor {
public:
    // Static utility methods that work with different data types
    template <typename T = DataType>
    static auto rotate(const T& data, int angle) {
        if constexpr (std::is_same_v<T, cv::Mat>) {
            // Implementation for cv::Mat
            return rotate_mat(data, angle);
        }
        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            // For raw buffer, need width, height and channels
            // This overload shouldn't be called directly - use the one with dimensions
            static_assert(!std::is_same_v<T, std::vector<uint8_t>>,
                "For vector<uint8_t>, use rotate(buffer, width, height, channels, angle)");
            return T{};
        }
        else {
            static_assert(std::is_same_v<T, void>, "Unsupported data type for rotation");
            return T{};
        }
    }

    static void rotate_inplace(unsigned char* data, int width, int height,
                          int channels, int angle, int& new_width, int& new_height) {
        if (angle == 0) {
            new_width = width;
            new_height = height;
            return;
        }

        bool swap_dimensions = (angle == 90 || angle == 270);
        new_width = swap_dimensions ? height : width;
        new_height = swap_dimensions ? width : height;

        cv::Mat src(height, width, CV_8UC(channels), data);
        cv::Mat dst(new_height, new_width, CV_8UC(channels));

        if (angle == 90) {
            cv::transpose(src, dst);
            cv::flip(dst, dst, 1);
        } else if (angle == 180) {
            cv::flip(src, dst, -1);
        } else if (angle == 270) {
            cv::transpose(src, dst);
            cv::flip(dst, dst, 0);
        }

        std::memcpy(data, dst.data, new_width * new_height * channels);
    }

    // Specific overload for std::vector<uint8_t>
    static std::vector<uint8_t> rotate(const std::vector<uint8_t>& buffer,
                                      int width, int height, int channels, int angle) {
        return rotate_buffer(buffer, width, height, channels, angle);
    }

    // Convert between types
    static cv::Mat to_mat(const std::vector<uint8_t>& buffer, int width, int height, int channels) {
        return cv::Mat(height, width, CV_8UC(channels), const_cast<uint8_t*>(buffer.data())).clone();
    }

    static std::vector<uint8_t> to_buffer(const cv::Mat& mat) {
        std::vector<uint8_t> buffer;
        if (mat.isContinuous()) {
            buffer.assign(mat.data, mat.data + mat.total() * mat.channels());
        } else {
            for (int i = 0; i < mat.rows; ++i) {
                buffer.insert(buffer.end(), mat.ptr<uint8_t>(i), mat.ptr<uint8_t>(i) + mat.cols * mat.channels());
            }
        }
        return buffer;
    }

private:
    static cv::Mat rotate_mat(const cv::Mat& image, int angle) {
        cv::Mat rotated;
        angle = ((angle % 360) + 360) % 360;

        if (angle == 0) {
            rotated = image.clone();
        } else if (angle == 90) {
            cv::transpose(image, rotated);
            cv::flip(rotated, rotated, 1);
        } else if (angle == 180) {
            cv::flip(image, rotated, -1);
        } else if (angle == 270) {
            cv::transpose(image, rotated);
            cv::flip(rotated, rotated, 0);
        } else {
            cv::Point2f center(static_cast<float>(image.cols / 2.0), static_cast<float>(image.rows / 2.0));
            cv::Mat rot_mat = cv::getRotationMatrix2D(center, angle, 1.0);
            cv::warpAffine(image, rotated, rot_mat, image.size());
        }
        return rotated;
    }

    static std::vector<uint8_t> rotate_buffer(const std::vector<uint8_t>& buffer,
                                             int width, int height, int channels, int angle) {
        cv::Mat mat = to_mat(buffer, width, height, channels);
        cv::Mat rotated = rotate_mat(mat, angle);
        return to_buffer(rotated);
    }
};

// 分发函数模板，用于将原始数据转换为不同的数据类型
template<typename DataType>
void dispatch_to_data_type(unsigned char* buf, int width, int height, DataType& data) {
    if constexpr (std::is_same_v<DataType, cv::Mat>) {
        // 对于cv::Mat，直接构造而不是memcpy
        data = cv::Mat(height, width, CV_8UC3, buf).clone();
    } else if constexpr (std::is_same_v<DataType, std::vector<uint8_t>>) {
        size_t bytes = width * height * 3; // 假设BGR格式
        data.resize(bytes);
        std::copy_n(buf, bytes, data.data());
    } else {
        static_assert(std::is_same_v<DataType, void>, "Unsupported data type for dispatch");
    }
}

} // namespace sensor::camera

#endif // SENSOR_CAMERA_UTILS_HPP