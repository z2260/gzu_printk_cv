#pragma once

#ifndef CONFIG_CONFIG_MANAGER_HPP
#define CONFIG_CONFIG_MANAGER_HPP

#include <memory>
#include <mutex>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <any>

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
        std::lock_guard<std::mutex> lock(mutex_);

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

        readers_[name] = reader;
        return reader;
    }

    std::shared_ptr<config::ConfigReaderBase<config::IniConfigReader>> createIniReader(
            const std::string& name, const std::string& file_path) {
        return createConfigReader<config::IniConfigReader>(name, file_path);
    }

    std::shared_ptr<config::ConfigReaderBase<config::JsonConfigReader>> createJsonReader(
            const std::string& name, const std::string& file_path) {
        return createConfigReader<config::JsonConfigReader>(name, file_path);
    }

    template<typename ReaderType>
    std::shared_ptr<ReaderType> getConfigReader(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = readers_.find(name);
        if (it != readers_.end()) {
            try {
                return std::any_cast<std::shared_ptr<ReaderType>>(it->second);
            } catch (const std::bad_any_cast&) {
                throw std::runtime_error("[Error] Configure reader type mismatch: " + name);
            }
        }
        throw std::runtime_error("[Error] The configuration reader does not exist: " + name);
    }

    void removeConfigReader(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        readers_.erase(name);
    }

private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    std::unordered_map<std::string, std::any> readers_;
    std::mutex mutex_;
};

} // namespace config

#endif // CONFIG_CONFIG_MANAGER_HPP