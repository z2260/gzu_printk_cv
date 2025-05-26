#pragma once

#ifndef CONFIG_INI_CONFIG_READER_HPP
#define CONFIG_INI_CONFIG_READER_HPP

#include <fstream>
#include <regex>
#include <unordered_map>

#include "config/config_reader_base.hpp"

namespace config {

class IniConfigReader : public ConfigReaderBase<IniConfigReader> {
public:
    IniConfigReader() = default;

    bool load_impl(const std::string& file_path) {
        file_path_ = file_path;
        sections_.clear();

        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        std::string current_section = "default";
        std::regex section_regex(R"(\s*\[(.*)\]\s*)");
        std::regex key_value_regex(R"(\s*([^=]+?)\s*=\s*(.*?)\s*$)");

        while (std::getline(file, line)) {
            size_t comment_pos = line.find(';');
            if (comment_pos != std::string::npos) {
                line = line.substr(0, comment_pos);
            }

            if (line.empty() || std::all_of(line.begin(), line.end(), isspace)) {
                continue;
            }

            std::smatch section_match;
            if (std::regex_match(line, section_match, section_regex)) {
                current_section = section_match[1].str();
                continue;
            }

            std::smatch key_value_match;
            if (std::regex_match(line, key_value_match, key_value_regex)) {
                std::string key = key_value_match[1].str();
                std::string value = key_value_match[2].str();

                sections_[current_section][key] = parse_value(value);
            }
        }

        return true;
    }

    bool reload_impl() {
        if (file_path_.empty()) {
            return false;
        }
        return load_impl(file_path_);
    }

    std::optional<ConfigValue> get_value_impl(const std::string& path) const {
        std::vector<std::string> parts = split_path(path);
        if (parts.empty()) {
            return std::nullopt;
        }

        std::string section = "default";
        std::string key;

        if (parts.size() == 1) {
            key = parts[0];
        } else {
            section = parts[0];
            key = parts[1];
        }

        auto section_it = sections_.find(section);
        if (section_it == sections_.end()) {
            return std::nullopt;
        }

        auto keyIt = section_it->second.find(key);
        if (keyIt == section_it->second.end()) {
            return std::nullopt;
        }

        return keyIt->second;
    }

    bool has_path_impl(const std::string& path) const {
        std::vector<std::string> parts = split_path(path);
        if (parts.empty()) {
            return false;
        }

        std::string section = "default";
        std::string key;

        if (parts.size() == 1) {
            key = parts[0];
        } else {
            section = parts[0];
            key = parts[1];
        }

        auto section_it = sections_.find(section);
        if (section_it == sections_.end()) {
            return false;
        }

        return section_it->second.find(key) != section_it->second.end();
    }

    std::string get_file_path_impl() const {
        return file_path_;
    }

    bool set_value_impl(const std::string& path, const ConfigValue& value) {
        std::vector<std::string> parts = split_path(path);
        if (parts.empty()) {
            return false;
        }

        std::string section = "default";
        std::string key;

        if (parts.size() == 1) {
            key = parts[0];
        } else {
            section = parts[0];
            key = parts[1];
        }

        sections_[section][key] = value;
        return true;
    }

    bool save_impl(const std::string& file_path = "") {
        std::string target_path = file_path.empty() ? file_path_ : file_path;
        if (target_path.empty()) {
            return false;
        }

        try {
            std::ofstream file(target_path);
            if (!file.is_open()) {
                return false;
            }

            auto default_it = sections_.find("default");
            if (default_it != sections_.end() && !default_it->second.empty()) {
                for (const auto& [key, value] : default_it->second) {
                    file << key << " = " << config_value_to_string(value) << std::endl;
                }
                file << std::endl;
            }

            for (const auto& [section, key_values] : sections_) {
                if (section == "default") {
                    continue;
                }

                file << "[" << section << "]" << std::endl;
                for (const auto& [key, value] : key_values) {
                    file << key << " = " << config_value_to_string(value) << std::endl;
                }
                file << std::endl;
            }

            return true;
        } catch (...) {
            return false;
        }
    }

private:
    std::string file_path_;
    std::unordered_map<std::string, std::unordered_map<std::string, ConfigValue>> sections_;

    std::vector<std::string> split_path(const std::string& path) const {
        std::vector<std::string> parts;
        std::istringstream stream(path);
        std::string part;

        while (std::getline(stream, part, CONFIG_PATH_SEPARATOR)) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }

        return parts;
    }

    ConfigValue parse_value(const std::string& value) const {
        if (value == "true" || value == "True" || value == "TRUE" || value == "yes" || value == "Yes" || value == "YES" || value == "1") {
            return true;
        }

        if (value == "false" || value == "False" || value == "FALSE" || value == "no" || value == "No" || value == "NO" || value == "0") {
            return false;
        }

        try {
            size_t pos;
            int int_value = std::stoi(value, &pos);
            if (pos == value.size()) {
                return int_value;
            }
        } catch (...) {}

        try {
            size_t pos;
            double double_value = std::stod(value, &pos);
            if (pos == value.size()) {
                return double_value;
            }
        } catch (...) {}

        if (value.find(',') != std::string::npos) {
            std::vector<std::string> elements;
            std::istringstream stream(value);
            std::string element;

            while (std::getline(stream, element, ',')) {
                element.erase(0, element.find_first_not_of(" \t"));
                element.erase(element.find_last_not_of(" \t") + 1);
                elements.push_back(element);
            }

            bool all_int = true;
            bool all_float = true;

            for (const auto& e : elements) {
                try {
                    size_t pos;
                    std::stoi(e, &pos);
                    if (pos != e.size()) {
                        all_int = false;
                    }
                } catch (...) {
                    all_int = false;
                }

                try {
                    size_t pos;
                    std::stod(e, &pos);
                    if (pos != e.size()) {
                        all_float = false;
                    }
                } catch (...) {
                    all_float = false;
                }
            }

            if (all_int) {
                std::vector<int> int_values;
                for (const auto& e : elements) {
                    int_values.push_back(std::stoi(e));
                }
                return int_values;
            }

            if (all_float) {
                std::vector<double> double_values;
                for (const auto& e : elements) {
                    double_values.push_back(std::stod(e));
                }
                return double_values;
            }

            return elements;
        }

        return value;
    }

    std::string config_value_to_string(const ConfigValue& value) const {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return arg;
            } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                return arg ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                std::string result;
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += arg[i];
                }
                return result;
            } else if constexpr (std::is_same_v<T, std::vector<int>>) {
                std::string result;
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += std::to_string(arg[i]);
                }
                return result;
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                std::string result;
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += std::to_string(arg[i]);
                }
                return result;
            } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
                std::string result;
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += arg[i] ? "true" : "false";
                }
                return result;
            } else {
                return "";
            }
        }, value);
    }
};

} // namespace config

#endif // CONFIG_INI_CONFIG_READER_HPP