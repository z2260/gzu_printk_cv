#pragma once

#ifndef CONFIG_INI_CONFIG_READER_HPP
#define CONFIG_INI_CONFIG_READER_HPP

#include <fstream>
#include <sstream>
#include <cctype>
#include <regex>
#include <unordered_map>
#include <string_view>
#include <array>
#include <algorithm>

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

            if (line.empty() || std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c); })) {
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
        std::vector<std::string_view> parts = split_path(path);
        if (parts.empty()) {
            return std::nullopt;
        }

        std::string section = "default";
        std::string key;

        if (parts.size() == 1) {
            key = std::string(parts[0]);
        } else {
            section = std::string(parts[0]);
            key = std::string(parts[1]);
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
        std::vector<std::string_view> parts = split_path(path);
        if (parts.empty()) {
            return false;
        }

        std::string section = "default";
        std::string key;

        if (parts.size() == 1) {
            key = std::string(parts[0]);
        } else {
            section = std::string(parts[0]);
            key = std::string(parts[1]);
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
        std::vector<std::string_view> parts = split_path(path);
        if (parts.empty()) {
            return false;
        }

        std::string section = "default";
        std::string key;

        if (parts.size() == 1) {
            key = std::string(parts[0]);
        } else {
            section = std::string(parts[0]);
            key = std::string(parts[1]);
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

    std::vector<std::string_view> split_path(std::string_view path) const {
        std::vector<std::string_view> parts;
        for(size_t b=0, e; b<path.size(); b=e+1){
            e = path.find_first_of(CONFIG_PATH_SEPARATOR, b);
            if(e == std::string_view::npos) e = path.size();
            if(e > b) { // 避免空字符串
                parts.emplace_back(path.substr(b, e-b));
            }
        }
        return parts;
    }

    ConfigValue parse_value(const std::string& value) const {
        if (value.empty()) {
            return std::string{};
        }

        // 布尔值转换表
        static constexpr std::array<std::pair<std::string_view, bool>, 8> bool_map = {{
            {"true", true}, {"false", false}, {"True", true}, {"False", false},
            {"TRUE", true}, {"FALSE", false}, {"1", true}, {"0", false}
        }};

        auto bool_it = std::find_if(bool_map.begin(), bool_map.end(),
            [&value](const auto& pair) { return pair.first == value; });
        if (bool_it != bool_map.end()) {
            return bool_it->second;
        }

        // 尝试解析为整数
        try {
            if (value.find('.') == std::string::npos) {
                return std::stoi(value);
            }
        } catch (...) {}

        // 尝试解析为浮点数
        try {
            return std::stod(value);
        } catch (...) {}

        // 默认作为字符串
        return value;
    }

    std::string config_value_to_string(const ConfigValue& value) const {
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else {
                return std::to_string(v);
            }
        }, value);
    }
};

} // namespace config

#endif // CONFIG_INI_CONFIG_READER_HPP