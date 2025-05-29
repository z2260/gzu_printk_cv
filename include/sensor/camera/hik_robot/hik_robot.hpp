#pragma once

#ifndef SENSOR_CAMERA_HIK_ROBOT_HPP
#define SENSOR_CAMERA_HIK_ROBOT_HPP

#include "sensor/camera/camera_base.hpp"
#include "sensor/camera/hik_robot/hik_robot_base.hpp"

#include "log/log_accessor.hpp"

#include "sensor/camera/hik_robot/hik_sdk/MvCameraControl.h"

#include <opencv2/opencv.hpp>

#include <atomic>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>
#include <optional>

namespace sensor::camera {

constexpr unsigned int kEnumLayerType  = MV_GIGE_DEVICE | MV_USB_DEVICE;
constexpr unsigned int kGrabTimeoutMs  = 1000;
constexpr unsigned int kDefaultNodeNum = 5;

template <HikRobotModel Model, typename DataType = std::vector<uint8_t>>
class HikRobot :
    public logger::LogAccessor          <HikRobot<Model, DataType>>,
    public CameraBase                   <HikRobot<Model, DataType>, DataType>,
    public CameraGainAccessor           <HikRobot<Model, DataType>>,
    public CameraExposureTimeAccessor   <HikRobot<Model, DataType>>,
    public CameraGammaAccessor          <HikRobot<Model, DataType>>,
    public CameraResolutionAccessor     <HikRobot<Model, DataType>>,
    public CameraFrameRateAccessor      <HikRobot<Model, DataType>>,
    public CameraBlackLevelAccessor     <HikRobot<Model, DataType>>
{
public:
    using CaptureCallback   = std::function<void(unsigned char*, MV_FRAME_OUT_INFO_EX*, void*)>;
    using ExceptionCallback = std::function<void(unsigned int, void*)>;
    using EventCallback     = std::function<void(MV_EVENT_OUT_INFO*, void*)>;

    /************** 工具宏 **************/
    #define CHECK_OPEN_RET(ret)                      \
        if (!is_init_ || !is_open_) {                \
            MERROR("SDK not init or device closed"); \
            return ret;                              \
        }

    #define CALL_SDK_RET(expr, ret) \
        do {                        \
            auto _rv = (expr);      \
            if (_rv != MV_OK) {     \
                MERROR("{} failed, err={}", #expr, _rv); \
                return ret;         \
            }                       \
        } while (0)

    HikRobot()  = default;

    ~HikRobot() {
        destroy();
    }

    /********************* SensorBase ************************/
    bool init_impl() {
        std::lock_guard<std::mutex> lk(state_mtx_);
        if (is_init_) return true;

        if (sdk_guard_.has_value() || MV_CC_Initialize() == MV_OK) {
            sdk_guard_.emplace();
        } else {
            MERROR("MV_CC_Initialize failed!");
            return false;
        }

        if (MV_CC_EnumDevices(kEnumLayerType, &device_list_) != MV_OK) {
            MERROR("MV_CC_EnumDevices failed!");
            return false;
        }

        device_count_ = device_list_.nDeviceNum;
        if (device_count_ == 0) {
            MERROR("No HIKRobot device detected!");
            return false;
        }

        for (unsigned i = 0; i < device_count_; ++i) {
            auto *info = device_list_.pDeviceInfo[i];
            MINFO("[{}] {} ({})",
                  i,
                  (info->nTLayerType == MV_GIGE_DEVICE)
                      ? reinterpret_cast<const char*>(info->SpecialInfo.stGigEInfo.chModelName)
                      : reinterpret_cast<const char*>(info->SpecialInfo.stUsb3VInfo.chModelName),
                  (info->nTLayerType == MV_GIGE_DEVICE ? "GigE" : "USB"));
        }

        is_init_ = true;
        return true;
    }

    bool open_impl() {
        return open_device(0);
    }

    bool open_impl(int index) {
        return open_device(index);
    }

    bool close_impl() { // NOLINT
        std::lock_guard<std::mutex> lk(state_mtx_);
        if (!is_open_) {
            return true;
        }
        device_handle_.reset();
        is_open_ = false;
        return true;
    }

    bool is_open_impl() {
        std::lock_guard<std::mutex> lk(state_mtx_);
        return is_open_;
    }

    bool get_data_impl(DataType &data) {
        return get_frame_impl(data);
    }

    DataType get_data_impl() {
        auto p = get_frame_impl(); return p ? *p : DataType{};
    }

    /********************* CameraBase ************************/
    bool start_capture_impl() {
        CHECK_OPEN_RET(false);

        std::lock_guard<std::mutex> lk(state_mtx_);
        if (is_capturing_) return true;

        CALL_SDK_RET(MV_CC_SetImageNodeNum(device_handle_.get(), kDefaultNodeNum), false);
        if (capture_callback_) {
            CALL_SDK_RET(MV_CC_RegisterImageCallBackEx(device_handle_.get(),
                                                       &HikRobot::internalCaptureCallback, this), false);
        }

        CALL_SDK_RET(MV_CC_StartGrabbing(device_handle_.get()), false);

        is_capturing_ = true;
        return true;
    }

    bool stop_capture_impl() {
        CHECK_OPEN_RET(false);

        std::lock_guard<std::mutex> lk(state_mtx_);
        if (!is_capturing_) return true;

        CALL_SDK_RET(MV_CC_StopGrabbing(device_handle_.get()), false);
        CALL_SDK_RET(MV_CC_RegisterImageCallBackEx(device_handle_.get(), nullptr, nullptr), false);

        is_capturing_ = false;
        return true;
    }

    bool is_captured_impl() {
        std::lock_guard<std::mutex> lk(state_mtx_);
        return is_capturing_;
    }

    /********************* 数据获取 ************************/
    bool get_frame_impl(DataType &data) {
        CHECK_OPEN_RET(false);
        MV_FRAME_OUT frame{};
        if (MV_CC_GetImageBuffer(device_handle_.get(), &frame, kGrabTimeoutMs) != MV_OK) {
            MWARN("MV_CC_GetImageBuffer timeout");
            return false;
        }

        FrameGuard guard{device_handle_.get(), frame};

        int dst_w = frame.stFrameInfo.nWidth;
        int dst_h = frame.stFrameInfo.nHeight;
        unsigned char *src = frame.pBufAddr;
        std::unique_ptr<unsigned char[]> rotated;

        if (rotation_ != 0 &&
            !rotate_image(frame.stFrameInfo, src, rotated, rotation_, dst_w, dst_h)) {
            return false;
        }

        if (rotated) {
            src = rotated.get();
        }

        std::unique_ptr<unsigned char[]> bgr;
        if (!convert_image(frame.stFrameInfo, src, bgr)) {
            return false;
        };

        dispatch_to_data_type(bgr.get(), dst_w, dst_h, data);
        return true;
    }

    std::shared_ptr<DataType> get_frame_impl() {
        auto ptr = std::make_shared<DataType>();
        return get_frame_impl(*ptr) ? ptr : nullptr;
    }

    /********************* 旋转 ************************/
    bool set_rotate_impl(uint8_t rotation) {
        if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
            MERROR("Invalid rotation angle {}", rotation);
            return false;
        }
        rotation_ = rotation;
        return true;
    }

    /********************* 回调注册 ************************/
    bool set_exception_callback(const ExceptionCallback &cb) {
        CHECK_OPEN_RET(false);
        exception_callback_ = cb;
        CALL_SDK_RET(MV_CC_RegisterExceptionCallBack(device_handle_.get(),
                       &HikRobot::internalExceptionCallback, this), false);
        return true;
    }

    bool set_capture_callback(const CaptureCallback &cb) {
        std::lock_guard<std::mutex> lk(state_mtx_);
        capture_callback_ = cb;
        return true;
    }

    bool register_event_callback(const char *event, const EventCallback &cb) {
        CHECK_OPEN_RET(false);
        event_cbs_[event] = cb;
        CALL_SDK_RET(MV_CC_RegisterEventCallBackEx(device_handle_.get(), event,
                                                   &HikRobot::internalEventCallback, this), false);
        return true;
    }

    bool unregister_event_callback(const char *event) {
        CHECK_OPEN_RET(false);
        if (event_cbs_.erase(event) == 0) return true;
        CALL_SDK_RET(MV_CC_RegisterEventCallBackEx(device_handle_.get(), event, nullptr, nullptr), false);
        return true;
    }

    bool enable_event_notification(const char *event) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_EventNotificationOn(device_handle_.get(), event), false);
        return true;
    }

    bool disable_event_notification(const char *event) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_EventNotificationOff(device_handle_.get(), event), false);
        return true;
    }

    bool set_event_node_num(unsigned int num) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_USB_SetEventNodeNum(device_handle_.get(), num), false);
        return true;
    }

    /********************* Gain Method ************************/
    float get_gain_impl() {
        CHECK_OPEN_RET(-1.0);
        MVCC_FLOATVALUE t = {};
        CALL_SDK_RET(MV_CC_GetFloatValue(device_handle_.get(), "Gain", &t), -1.0);
        return t.fCurValue;
    }

    bool get_gain_impl(float& gain_val) {
        float val = get_gain_impl();
        if (val == -1.0f) {
            return false;
        }
        gain_val = val;
        return true;
    }

    bool set_gain_impl(float gain_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetFloatValue(device_handle_.get(), "Gain", gain_val), false);
        return true;
    }

    /********************* ExposureTime Method ************************/
    float get_exposure_time_impl() {
        CHECK_OPEN_RET(-1.0);
        MVCC_FLOATVALUE t = {};
        CALL_SDK_RET(MV_CC_GetFloatValue(device_handle_.get(), "ExposureTime", &t), -1.0);
        return t.fCurValue;
    }

    bool get_exposure_time_impl(float& exposure_time_val) {
        CHECK_OPEN_RET(false);
        float val = get_exposure_time_impl();
        if (val < 0) {
            MERROR("Failed to get exposure time value");
            return false;
        }
        exposure_time_val = val;
        return true;
    }

    bool set_exposure_time_impl(float exposure_time_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetFloatValue(device_handle_.get(), "ExposureTime", exposure_time_val), false);
        return true;
    }

    /********************* Gamma Method ************************/
    float get_gamma_impl() {
        if (!get_gamma_enabled_impl()) {
            return -1.0;
        }
        CHECK_OPEN_RET(-1.0);
        MVCC_FLOATVALUE t = {};
        CALL_SDK_RET(MV_CC_GetFloatValue(device_handle_.get(), "Gamma", &t), -1.0);
        return t.fCurValue;
    }

    bool get_gamma_impl(float& gamma_val) {
        CHECK_OPEN_RET(false);
        float val = get_gamma_impl();
        if (val < 0) {
            MERROR("Failed to get gamma value");
            return false;
        }
        gamma_val = val;
        return true;
    }

    bool set_gamma_impl(float gamma_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetFloatValue(device_handle_.get(), "Gamma", gamma_val), false);
        return true;
    }

    bool get_gamma_enabled_impl() {
        CHECK_OPEN_RET(false);
        MVCC_ENUMVALUE t = {};
        CALL_SDK_RET(MV_CC_GetEnumValue(device_handle_.get(), "GammaEnable", &t), false);
        return t.nCurValue == 1;
    }

    bool set_gamma_enabled_impl(bool enabled_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetEnumValue(device_handle_.get(), "GammaEnable", enabled_val ? 1 : 0), false);
        return true;
    }

    /********************* FrameRate Method ************************/
    float get_max_frame_rate_impl() {
        CHECK_OPEN_RET(-1.0);
        MVCC_FLOATVALUE t = {};
        CALL_SDK_RET(MV_CC_GetFloatValue(device_handle_.get(), "AcquisitionFrameRate", &t), -1.0);
        return t.fCurValue;
    }

    bool get_max_frame_rate_impl(float& rate_val) {
        CHECK_OPEN_RET(false);
        float val = get_max_frame_rate_impl();
        if (val < 0) {
            MERROR("Failed to get frame rate");
            return false;
        }
        rate_val = val;
        return true;
    }

    bool set_max_frame_rate_impl(float rate_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetFloatValue(device_handle_.get(), "AcquisitionFrameRate", rate_val), false);
        return true;
    }

    /********************* BlackLevel Method ************************/
    float get_black_level_impl() {
        if (!get_black_level_enabled_impl()) {
            return -1.0;
        }
        CHECK_OPEN_RET(-1.0);
        MVCC_FLOATVALUE t = {};
        CALL_SDK_RET(MV_CC_GetFloatValue(device_handle_.get(), "BlackLevel", &t), -1.0);
        return t.fCurValue;
    }

    bool get_black_level_impl(float& level_val) {
        CHECK_OPEN_RET(false);
        float val = get_black_level_impl();
        if (val < 0) {
            MERROR("Failed to get black level value");
            return false;
        }
        level_val = val;
        return true;
    }

    bool set_black_level_impl(float level_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetFloatValue(device_handle_.get(), "BlackLevel", level_val), false);
        return true;
    }

    bool get_black_level_enabled_impl() {
        CHECK_OPEN_RET(false);
        MVCC_ENUMVALUE t = {};
        CALL_SDK_RET(MV_CC_GetEnumValue(device_handle_.get(), "BlackLevelEnable", &t), false);
        return t.nCurValue == 1;
    }

    bool set_black_level_enabled_impl(bool enabled_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetEnumValue(device_handle_.get(), "BlackLevelEnable", enabled_val ? 1 : 0), false);
        return true;
    }

    /********************* 分辨率获取 ************************/
    std::pair<int, int> get_resolution_impl() {
        CHECK_OPEN_RET(std::make_pair(0, 0));
        MVCC_INTVALUE width = {}, height = {};

        CALL_SDK_RET(MV_CC_GetIntValue(device_handle_.get(), "Width", &width), std::make_pair(0, 0));
        CALL_SDK_RET(MV_CC_GetIntValue(device_handle_.get(), "Height", &height), std::make_pair(0, 0));

        return std::make_pair(static_cast<int>(width.nCurValue), static_cast<int>(height.nCurValue));
    }

    bool get_resolution_impl(std::pair<int, int>& res_val) {
        CHECK_OPEN_RET(false);
        auto res = get_resolution_impl();
        if (res.first == 0 && res.second == 0) {
            MERROR("Failed to get resolution");
            return false;
        }
        res_val = res;
        return true;
    }

    bool set_resolution_impl(std::pair<int, int> res_val) {
        CHECK_OPEN_RET(false);
        CALL_SDK_RET(MV_CC_SetIntValue(device_handle_.get(), "Width", res_val.first), false);
        CALL_SDK_RET(MV_CC_SetIntValue(device_handle_.get(), "Height", res_val.second), false);
        return true;
    }


private:
    /************** RAII 小工具 **************/
    struct SdkGuard {
        ~SdkGuard() { MV_CC_Finalize(); }
    };

    struct DeviceHandle {
        void *ptr{nullptr};
        ~DeviceHandle() {
            if (ptr) MV_CC_DestroyHandle(ptr);
        }

        void reset() {
            if (ptr)
            {
                MV_CC_CloseDevice(ptr);
                MV_CC_DestroyHandle(ptr);
                ptr = nullptr;
            }
        }
        void *get() const {
            return ptr;
        }
    };

    struct FrameGuard {
        void *hdl;
        MV_FRAME_OUT &f;
        FrameGuard(void *h, MV_FRAME_OUT &fr) : hdl(h), f(fr) {}
        ~FrameGuard() { MV_CC_FreeImageBuffer(hdl, &f); }
    };

    /************** 设备打开 **************/
    bool open_device(int index) {
        if (!init_impl()) return false;
        if (index < 0 || index >= device_count_)
        {
            MERROR("Invalid index {}", index);
            return false;
        }

        std::lock_guard<std::mutex> lk(state_mtx_);
        if (is_open_) {
            MWARN("Already opened"); return true;
        }

        if (!MV_CC_IsDeviceAccessible(device_list_.pDeviceInfo[index], MV_ACCESS_Exclusive)) {
            MERROR("Device not accessible");
            return false;
        }

        if (MV_CC_CreateHandle(&device_handle_.ptr, device_list_.pDeviceInfo[index]) != MV_OK) {
            MERROR("CreateHandle failed");
            return false;
        }

        MV_CC_SetBayerCvtQuality(device_handle_.ptr, 1);
        MV_CC_SetBayerFilterEnable(device_handle_.ptr, 1);

        if (MV_CC_OpenDevice(device_handle_.ptr) != MV_OK) {
            MERROR("OpenDevice failed");
            device_handle_.reset();
            return false;
        }

        if (device_list_.pDeviceInfo[index]->nTLayerType == MV_GIGE_DEVICE) {
            int pkt = MV_CC_GetOptimalPacketSize(device_handle_.ptr);
            if (pkt > 0)
                MV_CC_SetIntValue(device_handle_.ptr, "GevSCPSPacketSize", pkt);
        }

        is_open_ = true;
        return true;
    }

    /************** 图像旋转 / 像素转换 **************/
    bool rotate_image(const MV_FRAME_OUT_INFO_EX &info,
                      unsigned char *src,
                      std::unique_ptr<unsigned char[]> &dst,
                      uint8_t angle,
                      int &out_w,
                      int &out_h)
    {
        if (angle == 0) return true;

        MV_IMG_ROTATION_ANGLE ang =
            (angle == 90)  ? MV_IMAGE_ROTATE_90  :
            (angle == 180) ? MV_IMAGE_ROTATE_180 :
                             MV_IMAGE_ROTATE_270;

        size_t buf_sz = std::max<size_t>(info.nFrameLen, info.nWidth * info.nHeight * 4);
        dst = std::make_unique<unsigned char[]>(buf_sz);

        MV_CC_ROTATE_IMAGE_PARAM p{};
        p.nWidth = info.nWidth; p.nHeight = info.nHeight;
        p.enPixelType    = info.enPixelType;
        p.pSrcData       = src;
        p.nSrcDataLen    = info.nFrameLen;
        p.pDstBuf        = dst.get();
        p.nDstBufSize    = buf_sz;
        p.enRotationAngle = ang;

        if (MV_CC_RotateImage(device_handle_.get(), &p) != MV_OK)
        {
            MERROR("Rotate failed");
            return false;
        }
        out_w = (angle == 90 || angle == 270) ? info.nHeight : info.nWidth;
        out_h = (angle == 90 || angle == 270) ? info.nWidth  : info.nHeight;
        return true;
    }

    bool convert_image(const MV_FRAME_OUT_INFO_EX &info,
                       unsigned char *src,
                       std::unique_ptr<unsigned char[]> &dst)
    {
        size_t dst_sz = info.nWidth * info.nHeight * 3;
        dst = std::make_unique<unsigned char[]>(dst_sz);

        MV_CC_PIXEL_CONVERT_PARAM_EX c{};
        c.nWidth = info.nWidth; c.nHeight = info.nHeight;
        c.pSrcData = src;  c.nSrcDataLen = info.nFrameLen;
        c.enSrcPixelType = info.enPixelType;
        c.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
        c.pDstBuffer = dst.get(); c.nDstBufferSize = dst_sz;

        if (MV_CC_ConvertPixelTypeEx(device_handle_.get(), &c) != MV_OK)
        {
            MERROR("Pixel convert failed");
            return false;
        }
        return true;
    }

    template<class T>
    void dispatch_to_data_type(unsigned char *buf, int w, int h, T &out) {
        constexpr int ch = 3;
        size_t bytes = w * h * ch;

        if constexpr (std::is_same_v<T, cv::Mat>) {
            out = cv::Mat(h, w, CV_8UC3);
            std::memcpy(out.data, buf, bytes);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<cv::Mat>>) {
            out = std::make_shared<cv::Mat>(h, w, CV_8UC3);
            std::memcpy(out->data, buf, bytes);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            out.resize(bytes);
            std::memcpy(out.data(), buf, bytes);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<std::vector<uint8_t>>>) {
            out = std::make_shared<std::vector<uint8_t>>(bytes);
            std::memcpy(out->data(), buf, bytes);
        } else if constexpr (std::is_pointer_v<T> &&
                           std::is_same_v<std::remove_pointer_t<T>, unsigned char>) {
            std::memcpy(out, buf, bytes);
        } else {
            static_assert(!std::is_same_v<T, T>, "Unsupported DataType");
        }
    }

    /************** 内部静态回调 **************/
    static void __stdcall internalCaptureCallback(unsigned char *p,
                                                  MV_FRAME_OUT_INFO_EX *info,
                                                  void *user)
    {
        auto *self = static_cast<HikRobot*>(user);
        if (!self || !self->capture_callback_) return;

        if (info->enPixelType == PixelType_Gvsp_BayerGR8  ||
            info->enPixelType == PixelType_Gvsp_BayerRG8  ||
            info->enPixelType == PixelType_Gvsp_BayerGB8  ||
            info->enPixelType == PixelType_Gvsp_BayerBG8)
        {
            size_t sz = info->nWidth * info->nHeight * 3;
            std::unique_ptr<unsigned char[]> bgr(new unsigned char[sz]);

            MV_CC_PIXEL_CONVERT_PARAM_EX c{};
            c.nWidth = info->nWidth; c.nHeight = info->nHeight;
            c.pSrcData = p; c.nSrcDataLen = info->nFrameLen;
            c.enSrcPixelType = info->enPixelType;
            c.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
            c.pDstBuffer = bgr.get(); c.nDstBufferSize = sz;

            MV_CC_SetBayerCvtQuality(self->device_handle_.get(), 1);
            MV_CC_SetBayerFilterEnable(self->device_handle_.get(), 1);

            if (MV_CC_ConvertPixelTypeEx(self->device_handle_.get(), &c) == MV_OK) {
                self->capture_callback_(bgr.get(), info, self->device_handle_.get());
                return;
            }
        }
        self->capture_callback_(p, info, self->device_handle_.get());
    }

    static void __stdcall internalEventCallback(MV_EVENT_OUT_INFO *e, void *user) {
        auto *self = static_cast<HikRobot*>(user);
        if (!self || !e) return;
        auto it = self->event_cbs_.find(e->EventName);
        if (it != self->event_cbs_.end()) it->second(e, self->device_handle_.get());
    }

    static void __stdcall internalExceptionCallback(unsigned int t, void *user) {
        auto *self = static_cast<HikRobot*>(user);
        if (self && self->exception_callback_) self->exception_callback_(t, self->device_handle_.get());
    }

    /************** 资源销毁 **************/
    void destroy() {
        stop_capture_impl();
        close_impl();
        sdk_guard_.reset();
        is_init_ = false;
    }

    /************** 成员变量 **************/
    std::optional<SdkGuard>  sdk_guard_;                ///< SDK资源管理（RAII）
    DeviceHandle             device_handle_;            ///< 设备句柄（RAII管理）
    MV_CC_DEVICE_INFO_LIST   device_list_{};            ///< 设备列表
    int                      device_count_{0};          ///< 设备数量

    std::atomic_bool         is_init_{false};         ///< 是否初始化
    std::atomic_bool         is_open_{false};         ///< 是否打开
    std::atomic_bool         is_capturing_{false};    ///< 是否正在捕获
    uint8_t                  rotation_{0};              ///< 旋转角度

    std::mutex               state_mtx_;                ///< 状态锁

    CaptureCallback          capture_callback_;         ///< 捕获回调
    ExceptionCallback        exception_callback_;       ///< 异常回调
    std::map<std::string, EventCallback> event_cbs_;    ///< 事件回调
};

} // namespace sensor::camera

#endif // SENSOR_CAMERA_HIK_ROBOT_HPP