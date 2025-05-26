#pragma once

#ifndef CONFIG_CONFIG_ACCESSOR_HPP
#define CONFIG_CONFIG_ACCESSOR_HPP

#include <string>
#include <memory>
#include <optional>
#include <cxxabi.h>

#include "config/config_manager.hpp"

namespace config {

template <typename T>
std::string get_type_name() {
    int status;
    char* demangled = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
    std::string result = (status == 0) ? demangled : typeid(T).name();
    std::free(demangled);
    return result;
}

template <typename Derived, typename ReaderType = IniConfigReader>
class ConfigAccessor {
public:
    static std::string config_name() {
        return get_type_name<Derived>();
    }

    static std::shared_ptr<ReaderType> get_config_reader() {
        return config_reader_;
    }

    static void init_config(const std::string& config_file_path) {
        std::string name = config_name();
        auto& config_manager = ConfigManager::getInstance();

        try {
            config_reader_ = config_manager.getConfigReader<ReaderType>(name);
        } catch (const std::runtime_error&) {
            config_reader_ = config_manager.createConfigReader<ReaderType>(name, config_file_path);
        }
    }

    template<typename T>
    static std::optional<T> get(const std::string& path) {
        auto value = get_config_reader()->get_value(path);
        return config::get_value<T>(value);
    }

    template<typename T>
    static T get_or_default(const std::string& path, const T& default_value) {
        auto value = get<T>(path);
        return value.has_value() ? value.value() : default_value;
    }

    template<typename T>
    static bool set(const std::string& path, const T& value) {
        return get_config_reader()->set_value(path, value);
    }

    static bool save() {
        return get_config_reader()->save();
    }

    static bool save(const std::string& file_path) {
        return get_config_reader()->save(file_path);
    }

    static bool has(const std::string& path) {
        return get_config_reader()->has_path(path);
    }

protected:
    ConfigAccessor() = default;
    ~ConfigAccessor() = default;

private:
    static std::shared_ptr<ReaderType> init_config_reader() {
        static std::string name = config_name();
        try {
            return ConfigManager::getInstance().getConfigReader<ReaderType>(name);
        } catch (const std::runtime_error&) {
            std::filesystem::create_directories("configs");
            std::string config_path = "configs/" + name +
                (std::is_same_v<ReaderType, IniConfigReader> ? ".ini" : ".json");
            return ConfigManager::getInstance().createConfigReader<ReaderType>(name, config_path);
        }
    }

    static inline std::shared_ptr<ReaderType> config_reader_ = init_config_reader();
};

} // namespace config

#endif // CONFIG_CONFIG_ACCESSOR_HPP