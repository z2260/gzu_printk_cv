#pragma once

#ifndef CONFIG_CONFIG_READER_BASE_HPP
#define CONFIG_CONFIG_READER_BASE_HPP

#include <string>
#include <variant>
#include <vector>
#include <optional>

namespace config {

using ConfigValue = std::variant<
    std::string,
    int,
    double,
    bool,
    std::vector<std::string>,
    std::vector<int>,
    std::vector<double>,
    std::vector<bool>
>;

constexpr char CONFIG_PATH_SEPARATOR = '.';

template <typename Derived>
class ConfigReaderBase {
public:
    ~ConfigReaderBase() = default;

    bool load(const std::string& file_path) {
        return static_cast<Derived*>(this)->load_impl(file_path);
    }

    bool reload() {
        return static_cast<Derived*>(this)->reload_impl();
    }

    std::optional<ConfigValue> get_value(const std::string& path) const {
        return static_cast<const Derived*>(this)->get_value_impl(path);
    }

    bool has_path(const std::string& path) const {
        return static_cast<const Derived*>(this)->has_path_impl(path);
    }

    std::string get_file_path() const {
        return static_cast<const Derived*>(this)->get_file_path_impl();
    }

    bool set_value(const std::string& path, const ConfigValue& value) {
        return static_cast<Derived*>(this)->set_value_impl(path, value);
    }

    bool save(const std::string& filePath = "") {
        return static_cast<Derived*>(this)->save_impl(filePath);
    }
};

template<typename T>
std::optional<T> get_value(const std::optional<ConfigValue>& config_value) {
    if (!config_value.has_value()) {
        return std::nullopt;
    }

    try {
        return std::get<T>(config_value.value());
    } catch (const std::bad_variant_access&) {
        return std::nullopt;
    }
}

template<>
inline std::optional<int> get_value<int>(const std::optional<ConfigValue>& config_value) {
    if (!config_value.has_value()) {
        return std::nullopt;
    }

    const ConfigValue& value = config_value.value();
    if (std::holds_alternative<int>(value)) {
        return std::get<int>(value);
    } else if (std::holds_alternative<double>(value)) {
        return static_cast<int>(std::get<double>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        try {
            return std::stoi(std::get<std::string>(value));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

template<>
inline std::optional<double> get_value<double>(const std::optional<ConfigValue>& config_value) {
    if (!config_value.has_value()) {
        return std::nullopt;
    }

    const ConfigValue& value = config_value.value();
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    } else if (std::holds_alternative<int>(value)) {
        return static_cast<double>(std::get<int>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        try {
            return std::stod(std::get<std::string>(value));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

template<>
inline std::optional<bool> get_value<bool>(const std::optional<ConfigValue>& config_value) {
    if (!config_value.has_value()) {
        return std::nullopt;
    }

    const ConfigValue& value = config_value.value();
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }

    if (std::holds_alternative<int>(value)) {
        return std::get<int>(value) != 0;
    }

    if (std::holds_alternative<std::string>(value)) {
        const std::string& strValue = std::get<std::string>(value);
        if (strValue == "true" || strValue == "True" || strValue == "TRUE" || strValue == "1") {
            return true;
        }

        if (strValue == "false" || strValue == "False" || strValue == "FALSE" || strValue == "0") {
            return false;
        }
    }

    return std::nullopt;
}

} // namespace config

#endif // CONFIG_CONFIG_READER_BASE_HPP