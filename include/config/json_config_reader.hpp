#pragma once

#ifndef CONFIG_JSON_CONFIG_READER_HPP
#define CONFIG_JSON_CONFIG_READER_HPP

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

#include "config/config_reader_base.hpp"

namespace config {
    
class JsonConfigReader : public ConfigReaderBase<JsonConfigReader> {
public:
    JsonConfigReader() = default;
    
    bool load_impl(const std::string& file_path) {
        file_path_ = file_path;
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        
        try {
            file >> json_;
            return true;
        } catch (const nlohmann::json::exception& e) {
            return false;
        }
    }
    
    bool reload_impl() {
        if (file_path_.empty()) {
            return false;
        }
        return load_impl(file_path_);
    }
    
    std::optional<ConfigValue> get_value_impl(const std::string& path) const {
        try {
            std::vector<std::string> parts = split_path(path);
            nlohmann::json current = json_;
            
            for (size_t i = 0; i < parts.size() - 1; ++i) {
                if (!current.contains(parts[i])) {
                    return std::nullopt;
                }
                current = current[parts[i]];
            }
            
            const std::string& last_part = parts.back();
            if (!current.contains(last_part)) {
                return std::nullopt;
            }
            
            const auto& value = current[last_part];
            return json_to_config_value(value);
        } catch (...) {
            return std::nullopt;
        }
    }
    
    bool has_path_impl(const std::string& path) const {
        try {
            std::vector<std::string> parts = split_path(path);
            nlohmann::json current = json_;
            
            for (const auto& part : parts) {
                if (!current.contains(part)) {
                    return false;
                }
                current = current[part];
            }
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    std::string get_file_path_impl() const {
        return file_path_;
    }
    
    bool set_value_impl(const std::string& path, const ConfigValue& value) {
        try {
            std::vector<std::string> parts = split_path(path);
            nlohmann::json* current = &json_;
            
            for (size_t i = 0; i < parts.size() - 1; ++i) {
                if (!current->contains(parts[i])) {
                    (*current)[parts[i]] = nlohmann::json::object();
                }
                current = &(*current)[parts[i]];
            }
            
            const std::string& last_part = parts.back();
            (*current)[last_part] = config_value_to_json(value);
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    bool save_impl(const std::string& filePath = "") {
        std::string target_path = filePath.empty() ? file_path_ : filePath;
        if (target_path.empty()) {
            return false;
        }
        
        try {
            std::ofstream file(target_path);
            if (!file.is_open()) {
                return false;
            }
            
            file << json_.dump(4);
            return true;
        } catch (...) {
            return false;
        }
    }
    
    const nlohmann::json& get_raw_json() const {
        return json_;
    }
    
private:
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
    
    ConfigValue json_to_config_value(const nlohmann::json& json) const {
        if (json.is_string()) {
            return json.get<std::string>();
        }

        if (json.is_number_integer()) {
            return json.get<int>();
        }
        
        if (json.is_number_float()) {
            return json.get<double>();
        }

        if (json.is_boolean()) {
            return json.get<bool>();
        }

        if (json.is_array()) {
            if (json.size() > 0) {
                if (json[0].is_string()) {
                    std::vector<std::string> values;
                    for (const auto& item : json) {
                        values.push_back(item.get<std::string>());
                    }
                    return values;
                }

                if (json[0].is_number_integer()) {
                    std::vector<int> values;
                    for (const auto& item : json) {
                        values.push_back(item.get<int>());
                    }
                    return values;
                }

                if (json[0].is_number_float()) {
                    std::vector<double> values;
                    for (const auto& item : json) {
                        values.push_back(item.get<double>());
                    }
                    return values;
                }

                if (json[0].is_boolean()) {
                    std::vector<bool> values;
                    for (const auto& item : json) {
                        values.push_back(item.get<bool>());
                    }
                    return values;
                }
            }
            return std::vector<std::string>{};
        }
        
        return std::string();
    }
    
    nlohmann::json config_value_to_json(const ConfigValue& value) const {
        return std::visit([](auto&& arg) -> nlohmann::json {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string> || 
                          std::is_same_v<T, int> || 
                          std::is_same_v<T, double> || 
                          std::is_same_v<T, bool>) {
                return arg;
            } else if constexpr (std::is_same_v<T, std::vector<std::string>> ||
                                std::is_same_v<T, std::vector<int>> ||
                                std::is_same_v<T, std::vector<double>> ||
                                std::is_same_v<T, std::vector<bool>>) {
                nlohmann::json array = nlohmann::json::array();
                for (const auto& item : arg) {
                    array.push_back(item);
                }
                return array;
            } else {
                return nullptr;
            }
        }, value);
    }
    
    nlohmann::json json_;
    std::string file_path_;
};

} // namespace config

#endif // CONFIG_JSON_CONFIG_READER_HPP