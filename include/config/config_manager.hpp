#pragma once

#ifndef CONFIG_CONFIG_MANAGER_HPP
#define CONFIG_CONFIG_MANAGER_HPP

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <typeinfo>

#include "config/config_reader_base.hpp"
#include "config/ini_config_reader.hpp"
#include "config/json_config_reader.hpp"

namespace config {

class ConfigManager {
public:
    static ConfigManager& getInstance() {
        static ConfigManager instance;
        return instance;
    }

    template<typename ReaderType>
    std::shared_ptr<ReaderType> createConfigReader(const std::string& name, const std::string& file_path) {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        if (readers_.count(name) > 0) {
            throw std::runtime_error("[Error] The configuration reader already exists: " + name);
        }

        auto reader = std::make_shared<ReaderType>();
        if (!file_path.empty()) {
            std::filesystem::path p(file_path);
            std::filesystem::create_directories(p.parent_path());

            if (!reader->load(file_path)) {
                if (!std::filesystem::exists(file_path)) {
                    reader->save(file_path);
                } else {
                    throw std::runtime_error("[Error] Unable to load configuration file: " + file_path);
                }
            }
        }

        readers_[name] = std::static_pointer_cast<void>(reader);
        reader_types_[name] = typeid(ReaderType).hash_code();
        return reader;
    }

    std::shared_ptr<IniConfigReader> createIniReader(
            const std::string& name, const std::string& file_path) {
        return createConfigReader<IniConfigReader>(name, file_path);
    }

    std::shared_ptr<JsonConfigReader> createJsonReader(
            const std::string& name, const std::string& file_path) {
        return createConfigReader<JsonConfigReader>(name, file_path);
    }

    template<typename ReaderType>
    std::shared_ptr<ReaderType> getConfigReader(const std::string& name) {
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            auto it = readers_.find(name);
            if (it != readers_.end()) {
                // 类型安全检查
                auto type_it = reader_types_.find(name);
                if (type_it != reader_types_.end() && type_it->second == typeid(ReaderType).hash_code()) {
                    return std::static_pointer_cast<ReaderType>(it->second);
                } else {
                    throw std::runtime_error("[Error] Configure reader type mismatch: " + name);
                }
            }
        }
        throw std::runtime_error("[Error] The configuration reader does not exist: " + name);
    }

    void removeConfigReader(const std::string& name) {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        readers_.erase(name);
        reader_types_.erase(name);
    }

private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    std::unordered_map<std::string, std::shared_ptr<void>> readers_;
    std::unordered_map<std::string, std::size_t> reader_types_; // 存储类型hash用于类型检查
    mutable std::shared_mutex mutex_;
};

} // namespace config

#endif // CONFIG_CONFIG_MANAGER_HPP