#pragma once

#ifndef SENSOR_CAMERA_ACCESSOR_HPP
#define SENSOR_CAMERA_ACCESSOR_HPP

#include "traits/type_traits.hpp"

namespace sensor::camera {

/**************************************************
* Gain Accessor
***************************************************/
template <typename Derived, typename GainType = float>
class CameraGainAccessor {
public:
    GainType get_gain() {
        static_assert(traits::is_callable_r_v<
                          GainType,
                          decltype(static_cast<GainType (Derived::*)()>(&Derived::get_gain_impl)),
                          Derived&>,
                      "Derived must implement: DataType get_data_impl()");
        return static_cast<Derived*>(this)->get_gain_impl();
    }

    bool get_gain(GainType& gain_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(GainType&)>(&Derived::get_gain_impl)),
                          Derived&, GainType&>,
                      "Derived must implement: bool get_data_impl(DataType&)");
        return static_cast<Derived*>(this)->get_gain_impl(gain_val);
    }


    bool set_gain(GainType gain_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(GainType)>(&Derived::set_gain_impl)),
                          Derived&, GainType&>,
                      "Derived must implement: bool set_gain_impl(DataType&)");
        return static_cast<Derived*>(this)->set_gain_impl(gain_val);
    }


};

/**************************************************
* ExposureTime Accessor
***************************************************/
template <typename Derived, typename ExposureTimeType = float>
class CameraExposureTimeAccessor {
public:
    ExposureTimeType get_exposure_time() {
        static_assert(traits::is_callable_r_v<
                          ExposureTimeType,
                          decltype(static_cast<ExposureTimeType (Derived::*)()>(&Derived::get_exposure_time_impl)),
                          Derived&>,
                      "Derived must implement: ExposureTimeType get_exposure_time_impl()");
        return static_cast<Derived*>(this)->get_exposure_time_impl();
    }

    bool get_exposure_time(ExposureTimeType& exposure_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(ExposureTimeType&)>(&Derived::get_exposure_time_impl)),
                          Derived&, ExposureTimeType&>,
                      "Derived must implement: bool get_exposure_time_impl(ExposureTimeType&)");
        return static_cast<Derived*>(this)->get_exposure_time_impl(exposure_val);
    }

    bool set_exposure_time(ExposureTimeType exposure_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(ExposureTimeType)>(&Derived::set_exposure_time_impl)),
                          Derived&, ExposureTimeType&>,
                      "Derived must implement: bool set_exposure_time_impl(ExposureTimeType&)");
        return static_cast<Derived*>(this)->set_exposure_time_impl(exposure_val);
    }
};

/**************************************************
* Gamma Accessor
***************************************************/
template <typename Derived, typename GammaType = float>
class CameraGammaAccessor {
public:
    GammaType get_gamma() {
        if (!this->get_gamma_enabled()) {
            return static_cast<GammaType>(-1.0); // Return -1 if gamma is not enabled
        }
        static_assert(traits::is_callable_r_v<
                          GammaType,
                          decltype(static_cast<GammaType (Derived::*)()>(&Derived::get_gamma_impl)),
                          Derived&>,
                      "Derived must implement: GammaType get_gamma_impl()");
        return static_cast<Derived*>(this)->get_gamma_impl();
    }

    bool get_gamma(GammaType& gamma_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(GammaType&)>(&Derived::get_gamma_impl)),
                          Derived&, GammaType&>,
                      "Derived must implement: bool get_gamma_impl(GammaType&)");
        return static_cast<Derived*>(this)->get_gamma_impl(gamma_val);
    }

    bool set_gamma(GammaType gamma_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(GammaType)>(&Derived::set_gamma_impl)),
                          Derived&, GammaType&>,
                      "Derived must implement: bool set_gamma_impl(GammaType&)");
        return static_cast<Derived*>(this)->set_gamma_impl(gamma_val);
    }

    bool set_gamma_enabled(bool enabled_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(bool)>(&Derived::set_gamma_enabled_impl)),
                          Derived&, bool>,
                      "Derived must implement: bool set_gamma_enabled_impl(bool)");
        return static_cast<Derived*>(this)->set_gamma_enabled_impl(enabled_val);
    }

    bool get_gamma_enabled() {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)()>(&Derived::get_gamma_enabled_impl)),
                          Derived&>,
                      "Derived must implement: bool get_gamma_enabled_impl()");
        return static_cast<Derived*>(this)->get_gamma_enabled_impl();
    }

};

/**************************************************
* Resolution Accessor
***************************************************/
template <typename Derived, typename ResolutionType = std::pair<int, int>>
class CameraResolutionAccessor {
public:
    ResolutionType get_resolution() {
        static_assert(traits::is_callable_r_v<
                          ResolutionType,
                          decltype(static_cast<ResolutionType (Derived::*)()>(&Derived::get_resolution_impl)),
                          Derived&>,
                      "Derived must implement: ResolutionType get_resolution_impl()");
        return static_cast<Derived*>(this)->get_resolution_impl();
    }

    bool get_resolution(ResolutionType& res_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(ResolutionType&)>(&Derived::get_resolution_impl)),
                          Derived&, ResolutionType&>,
                      "Derived must implement: bool get_resolution_impl(ResolutionType&)");
        return static_cast<Derived*>(this)->get_resolution_impl(res_val);
    }

    bool set_resolution(ResolutionType res_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(ResolutionType)>(&Derived::set_resolution_impl)),
                          Derived&, ResolutionType&>,
                      "Derived must implement: bool set_resolution_impl(ResolutionType&)");
        return static_cast<Derived*>(this)->set_resolution_impl(res_val);
    }
};

/**************************************************
* Max Frame Rate Accessor
***************************************************/
template <typename Derived, typename FrameRateType = float>
class CameraFrameRateAccessor {
public:
    FrameRateType get_max_frame_rate() {
        static_assert(traits::is_callable_r_v<
                          FrameRateType,
                          decltype(static_cast<FrameRateType (Derived::*)()>(&Derived::get_max_frame_rate_impl)),
                          Derived&>,
                      "Derived must implement: FrameRateType get_max_frame_rate_impl()");
        return static_cast<Derived*>(this)->get_max_frame_rate_impl();
    }

    bool get_max_frame_rate(FrameRateType& rate_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(FrameRateType&)>(&Derived::get_max_frame_rate_impl)),
                          Derived&, FrameRateType&>,
                      "Derived must implement: bool get_max_frame_rate_impl(FrameRateType&)");
        return static_cast<Derived*>(this)->get_max_frame_rate_impl(rate_val);
    }

    bool set_max_frame_rate(FrameRateType rate_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(FrameRateType)>(&Derived::set_max_frame_rate_impl)),
                          Derived&, FrameRateType&>,
                      "Derived must implement: bool set_max_frame_rate_impl(FrameRateType&)");
        return static_cast<Derived*>(this)->set_max_frame_rate_impl(rate_val);
    }
};

/**************************************************
* Black Level  Accessor
***************************************************/
template <typename Derived, typename BlackLevelType = float>
class CameraBlackLevelAccessor {
public:
    BlackLevelType get_black_level() {
        static_assert(traits::is_callable_r_v<
                          BlackLevelType,
                          decltype(static_cast<BlackLevelType (Derived::*)()>(&Derived::get_black_level_impl)),
                          Derived&>,
                      "Derived must implement: BlackLevelType get_black_level_impl()");
        return static_cast<Derived*>(this)->get_black_level_impl();
    }

    bool get_black_level(BlackLevelType& level_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(BlackLevelType&)>(&Derived::get_black_level_impl)),
                          Derived&, BlackLevelType&>,
                      "Derived must implement: bool get_black_level_impl(BlackLevelType&)");
        return static_cast<Derived*>(this)->get_black_level_impl(level_val);
    }

    bool set_black_level(BlackLevelType level_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(BlackLevelType)>(&Derived::set_black_level_impl)),
                          Derived&, BlackLevelType&>,
                      "Derived must implement: bool set_black_level_impl(BlackLevelType&)");
        return static_cast<Derived*>(this)->set_black_level_impl(level_val);
    }

    bool set_black_level_enabled(bool enabled_val) {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)(bool)>(&Derived::set_black_level_enabled_impl)),
                          Derived&, bool>,
                      "Derived must implement: bool set_black_level_enabled_impl(bool)");
        return static_cast<Derived*>(this)->set_black_level_enabled_impl(enabled_val);
    }

    bool get_black_level_enabled() {
        static_assert(traits::is_callable_r_v<
                          bool,
                          decltype(static_cast<bool (Derived::*)()>(&Derived::get_black_level_enabled_impl)),
                          Derived&>,
                      "Derived must implement: bool get_black_level_enabled_impl()");
        return static_cast<Derived*>(this)->get_black_level_enabled_impl();
    }
};

} // namespace sensor::camera

#endif // SENSOR_CAMERA_ACCESSOR_HPP