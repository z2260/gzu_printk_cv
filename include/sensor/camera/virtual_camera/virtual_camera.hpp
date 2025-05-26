#pragma once
#ifndef VIRTUAL_CAMERA_HPP
#define VIRTUAL_CAMERA_HPP

#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <opencv2/opencv.hpp>

#include "sensor/camera/camera_base.hpp"
#include "sensor/camera/virtual_camera/virtual_camera_base.hpp"

#include "log/log_accessor.hpp"

namespace sensor::camera {

template <VideoSourceType SourceType>
class VirtualCamera :
    public logger::LogAccessor<VirtualCamera<SourceType>>,
    public CameraBase<VirtualCamera<SourceType>, cv::Mat>,
    public CameraResolutionAccessor<VirtualCamera<SourceType>>,
    public CameraFrameRateAccessor<VirtualCamera<SourceType>, int>
{
public:
    using traits = VideoSourceTypeTraits<SourceType>;
    using source_type = typename traits::source_type;
    using source_data_type = typename traits::data_type;
    using output_data_type = cv::Mat;
    using clock_type = std::chrono::steady_clock;

    VirtualCamera() = default;
    explicit VirtualCamera(const source_type& source) : source_(source) {}

    bool init_impl() {
        MINFO("Initializing virtual camera");
        initialized_ = true;
        return initialized_;
    }

    bool open_impl() {
        if (!initialized_) {
            MERROR("Camera not initialized");
            return false;
        }

        MINFO("Opening virtual camera");
        opened_ = traits::open(source_, source_data_);

        if (opened_) {
            current_fps_ = traits::get_fps(source_data_);
            auto res = traits::get_resolution(source_data_);
            last_resolution_ = res;
            MINFO("Camera opened, resolution: {}x{}, frame rate: {}",
                           res.first, res.second, current_fps_);
        } else {
            MERROR("Failed to open camera");
        }

        return opened_;
    }

    bool close_impl() {
        if (!opened_) {
            MWARN("Camera already closed");
            return true;
        }

        if (is_capture_) {
            stop_capture_impl();
        }

        MINFO("Closing virtual camera");
        traits::close(source_data_);
        opened_ = false;
        return true;
    }

    bool is_open_impl() const {
        return opened_;
    }

    bool get_data_impl(output_data_type& data) {
        if (!opened_) {
            data = output_data_type();
            MERROR("Camera not opened, cannot get data");
            return false;
        }

        if (current_fps_ > 0) {
            auto now = clock_type::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_frame_time_).count();

            int frame_interval = 1000 / current_fps_;
            if (elapsed < frame_interval) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(frame_interval - elapsed));
            }
            last_frame_time_ = clock_type::now();
        }

        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!traits::get_frame(source_data_, data)) {
            MERROR("Failed to get frame");
            return false;
        }

        if (!data.empty()) {
            if (data.cols != last_resolution_.first || data.rows != last_resolution_.second) {
                cv::resize(data, data, cv::Size(last_resolution_.first, last_resolution_.second));
            }
        }

        return true;
    }

    output_data_type get_data_impl() {
        output_data_type data;
        get_data_impl(data);
        return data;
    }

    bool get_frame_impl(output_data_type& data) {
        return get_data_impl(data);
    }

    std::shared_ptr<output_data_type> get_frame_impl() {
        output_data_type data;
        get_data_impl(data);
        return std::make_shared<output_data_type>(std::move(data));
    }

    bool start_capture_impl() {
        if (!initialized_) {
            MERROR("Camera not initialized, cannot start capture");
            return false;
        }
        if (!opened_) {
            MERROR("Camera not opened, cannot start capture");
            return false;
        }

        output_data_type frame;
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!traits::get_frame(source_data_, frame)) {
            MERROR("Cannot get frame");
            return false;
        }

        MINFO("Starting capture");
        last_frame_ = frame;
        last_frame_time_ = clock_type::now();
        is_capture_ = true;
        return true;
    }

    bool stop_capture_impl() {
        if (!initialized_ || !opened_) {
            MERROR("Camera not initialized or opened, cannot stop capture");
            return false;
        }

        if (!is_capture_) {
            MWARN("Camera not currently capturing");
            return true;
        }

        MINFO("Stopping capture");
        is_capture_ = false;
        return true;
    }

    bool is_captured_impl() {
        return is_capture_;
    }

    bool set_source(const source_type& source) {
        if (opened_) {
            MINFO("Closing camera before changing source");
            close_impl();
        }

        MINFO("Setting new video source");
        source_ = source;
        return true;
    }

    std::pair<int, int> get_resolution_impl() {
        if (!opened_) {
            MWARN("Camera not opened, cannot get resolution");
            return {0, 0};
        }
        return last_resolution_;
    }

    bool get_resolution_impl(std::pair<int, int>& res_val) {
        if (!opened_) {
            res_val = {0, 0};
            MWARN("Camera not opened, cannot get resolution");
            return false;
        }
        res_val = last_resolution_;
        return true;
    }

    bool set_resolution_impl(std::pair<int, int> res_val) {
        MINFO("Attempting to set resolution: {}x{}", res_val.first, res_val.second);
        if (!opened_) {
            MERROR("Camera not opened, cannot set resolution");
            return false;
        }

        if (res_val.first <= 0 || res_val.second <= 0) {
            MERROR("Invalid resolution: {}x{}", res_val.first, res_val.second);
            return false;
        }

        last_resolution_ = res_val;
        MINFO("Resolution set successfully: {}x{}", res_val.first, res_val.second);
        return true;
    }

    int get_max_frame_rate_impl() {
        if (!opened_) {
            MWARN("Camera not opened, cannot get frame rate");
            return 0;
        }
        return current_fps_;
    }

    bool get_max_frame_rate_impl(int& rate_val) {
        if (!opened_) {
            rate_val = 0;
            MWARN("Camera not opened, cannot get frame rate");
            return false;
        }
        rate_val = current_fps_;
        return true;
    }

    bool set_max_frame_rate_impl(int& rate_val) {
        MINFO("Attempting to set maximum frame rate: {}", rate_val);
        if (!opened_) {
            MERROR("Camera not opened, cannot set frame rate");
            return false;
        }

        if (rate_val <= 0) {
            MERROR("Invalid frame rate: {}", rate_val);
            return false;
        }

        current_fps_ = rate_val;
        MINFO("Frame rate set successfully: {}", rate_val);
        return true;
    }

    bool is_initialized() const {
        return initialized_;
    }

    std::string get_last_error() const {
        return last_error_;
    }

private:
    source_type source_;
    source_data_type source_data_;
    output_data_type last_frame_;
    bool initialized_ = false;
    bool opened_ = false;
    bool is_capture_ = false;

    int current_fps_ = 30;
    std::pair<int, int> last_resolution_ = {640, 480};
    std::mutex frame_mutex_;
    std::string last_error_;

    std::chrono::time_point<clock_type> last_frame_time_ = clock_type::now();
};

using ImageCamera = VirtualCamera<VideoSourceType::IMAGE>;
using VideoCamera = VirtualCamera<VideoSourceType::VIDEO>;

} // namespace sensor::camera

#endif // VIRTUAL_CAMERA_HPP