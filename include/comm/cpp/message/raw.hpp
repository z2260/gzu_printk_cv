#pragma once

#ifndef COMM_MESSAGE_RAW_HPP
#define COMM_MESSAGE_RAW_HPP

#include "../core/traits.hpp"
#include "log/log_accessor.hpp"
#include <optional>
#include <vector>
#include <functional>
#include <type_traits>
#include <cstring>
#include <string>
#include <unordered_map>
#include <typeinfo>
#include <memory>
#include <atomic>

#include <nlohmann/json.hpp>

namespace comm::message {

// Stable type ID system to replace typeid().name()
class TypeRegistry {
public:
    template<typename T>
    static std::uint32_t get_type_id() {
        static std::uint32_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    template<typename T>
    static void register_type(std::uint32_t id, const std::string& name) {
        auto& registry = get_registry();
        registry[id] = name;
    }

    static std::string get_type_name(std::uint32_t id) {
        auto& registry = get_registry();
        auto it = registry.find(id);
        return it != registry.end() ? it->second : "unknown_type_" + std::to_string(id);
    }

private:
    static std::atomic<std::uint32_t> next_id_;
    static std::unordered_map<std::uint32_t, std::string>& get_registry() {
        static std::unordered_map<std::uint32_t, std::string> registry;
        return registry;
    }
};

// Helper macro for type registration
#define COMM_REGISTER_TYPE(Type, ID) \
    namespace { \
        struct Type##_registrar { \
            Type##_registrar() { \
                comm::message::TypeRegistry::register_type<Type>(ID, #Type); \
            } \
        }; \
        static Type##_registrar Type##_reg_instance; \
    }

template<typename Derived>
class RawMessageBase : public logger::LogAccessor<Derived> {
public:
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }
    const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode(const T& obj) {
        MTRACE("Starting encode for type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));
        auto result = derived().template encode_impl<T>(obj);
        if (result) {
            MDEBUG("Successfully encoded {} bytes", result->size());
        } else {
            MWARN("Failed to encode object of type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));
        }
        return result;
    }

    template<typename Handler>
    void dispatch(traits::buffer_view<const std::uint8_t> data, Handler&& handler) {
        MTRACE("Dispatching message with {} bytes", data.size());
        derived().dispatch_impl(data, std::forward<Handler>(handler));
        MDEBUG("Message dispatch completed");
    }

protected:
    RawMessageBase() = default;
    ~RawMessageBase() = default;

    RawMessageBase(const RawMessageBase&) = delete;
    RawMessageBase& operator=(const RawMessageBase&) = delete;
    RawMessageBase(RawMessageBase&&) = delete;
    RawMessageBase& operator=(RawMessageBase&&) = delete;
};

class RawBytes : public RawMessageBase<RawBytes> {
public:
    RawBytes() {
        MINFO("RawBytes initialized");
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_impl(const T& obj) {
        MTRACE("RawBytes encoding type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));

        if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
            MDEBUG("Encoding vector<uint8_t> with {} bytes", obj.size());
            ++stats_.messages_encoded;
            stats_.bytes_encoded += obj.size();
            return obj;
        } else if constexpr (std::is_same_v<T, traits::buffer_view<const std::uint8_t>>) {
            MDEBUG("Encoding buffer_view with {} bytes", obj.size());
            ++stats_.messages_encoded;
            stats_.bytes_encoded += obj.size();
            return std::vector<std::uint8_t>(obj.begin(), obj.end());
        } else if constexpr (std::is_same_v<T, std::string>) {
            MDEBUG("Encoding string with {} bytes", obj.size());
            ++stats_.messages_encoded;
            stats_.bytes_encoded += obj.size();
            return std::vector<std::uint8_t>(obj.begin(), obj.end());
        } else if constexpr (std::is_trivially_copyable_v<T>) {
            MDEBUG("Encoding trivially copyable type, size: {} bytes", sizeof(T));
            std::vector<std::uint8_t> result(sizeof(T));
            std::memcpy(result.data(), &obj, sizeof(T));
            ++stats_.messages_encoded;
            stats_.bytes_encoded += sizeof(T);
            return result;
        } else {
            MERROR("Unsupported type for RawBytes encoding: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));
            static_assert(traits::dependent_false_v<T>, "Unsupported type for RawBytes encoding");
            return std::nullopt;
        }
    }

    template<typename Handler>
    void dispatch_impl(traits::buffer_view<const std::uint8_t> data, Handler&& handler) {
        MTRACE("RawBytes dispatching {} bytes", data.size());
        ++stats_.messages_decoded;
        stats_.bytes_decoded += data.size();
        handler(std::vector<std::uint8_t>(data.begin(), data.end()));
        MDEBUG("RawBytes dispatch completed");
    }

    struct Stats {
        std::size_t messages_encoded = 0;
        std::size_t messages_decoded = 0;
        std::size_t bytes_encoded = 0;
        std::size_t bytes_decoded = 0;
    };

    Stats get_stats() const noexcept {
        MTRACE("Getting stats: encoded={}, decoded={}, bytes_in={}, bytes_out={}",
               stats_.messages_encoded, stats_.messages_decoded, stats_.bytes_encoded, stats_.bytes_decoded);
        return stats_;
    }

    void reset_stats() noexcept {
        MINFO("Resetting RawBytes statistics");
        stats_ = Stats{};
    }

private:
    mutable Stats stats_;
};

class TypedMessage : public RawMessageBase<TypedMessage> {
public:
    TypedMessage() {
        MINFO("TypedMessage initialized");
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_impl(const T& obj) {
        MTRACE("TypedMessage encoding type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));

        std::vector<std::uint8_t> result;
        std::uint32_t type_id = TypeRegistry::get_type_id<T>();
        MDEBUG("Type ID for {}: {}", TypeRegistry::get_type_name(type_id), type_id);

        result.resize(sizeof(type_id));
        std::memcpy(result.data(), &type_id, sizeof(type_id));
        std::uint32_t data_size = 0;

        if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
            data_size = static_cast<std::uint32_t>(obj.size());
            MDEBUG("Encoding vector<uint8_t> with {} bytes", data_size);
            result.resize(result.size() + sizeof(data_size));
            std::memcpy(result.data() + sizeof(type_id), &data_size, sizeof(data_size));
            result.insert(result.end(), obj.begin(), obj.end());
            return result;
        } else if constexpr (std::is_same_v<T, std::string>) {
            data_size = static_cast<std::uint32_t>(obj.size());
            MDEBUG("Encoding string with {} bytes", data_size);
            result.resize(result.size() + sizeof(data_size));
            std::memcpy(result.data() + sizeof(type_id), &data_size, sizeof(data_size));
            result.insert(result.end(), obj.begin(), obj.end());
            return result;
        } else if constexpr (std::is_trivially_copyable_v<T>) {
            data_size = sizeof(T);
            MDEBUG("Encoding trivially copyable type, size: {} bytes", data_size);
            result.resize(result.size() + sizeof(data_size) + sizeof(T));
            std::memcpy(result.data() + sizeof(type_id), &data_size, sizeof(data_size));
            std::memcpy(result.data() + sizeof(type_id) + sizeof(data_size), &obj, sizeof(T));
            return result;
        } else {
            MERROR("Complex types need custom serialization for type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));
            static_assert(traits::dependent_false_v<T>, "Complex types need custom serialization");
            return std::nullopt;
        }
    }

    template<typename Handler>
    void dispatch_impl(traits::buffer_view<const std::uint8_t> data, Handler&& handler) {
        MTRACE("TypedMessage dispatching {} bytes", data.size());

        if (data.size() < sizeof(std::uint32_t) * 2) {
            MWARN("Insufficient data for TypedMessage header: {} bytes", data.size());
            return;
        }

        std::uint32_t type_id;
        std::memcpy(&type_id, data.data(), sizeof(type_id));

        std::uint32_t data_size;
        std::memcpy(&data_size, data.data() + sizeof(type_id), sizeof(data_size));

        MDEBUG("Received message: type_id={}, data_size={}", type_id, data_size);

        constexpr std::uint32_t MAX_MESSAGE_SIZE = 64 * 1024 * 1024; // 64MB
        if (data_size > MAX_MESSAGE_SIZE) {
            MWARN("Message size {} exceeds maximum {}", data_size, MAX_MESSAGE_SIZE);
            return;
        }

        if (data.size() < sizeof(type_id) + sizeof(data_size) + data_size) {
            MWARN("Data size mismatch: expected {}, got {}",
                  sizeof(type_id) + sizeof(data_size) + data_size, data.size());
            return;
        }

        auto payload_start = data.data() + sizeof(type_id) + sizeof(data_size);
        traits::buffer_view<const std::uint8_t> payload(payload_start, data_size);

        dispatch_by_type_id(type_id, payload, handler);
        MDEBUG("TypedMessage dispatch completed");
    }

    template<typename T, typename Handler>
    void register_handler(Handler&& handler) {
        std::uint32_t type_id = TypeRegistry::get_type_id<T>();
        MINFO("Registering handler for type: {}, id: {}", TypeRegistry::get_type_name(type_id), type_id);

        handlers_[type_id] = [handler = std::forward<Handler>(handler)](traits::buffer_view<const std::uint8_t> data) {
            if constexpr (std::is_same_v<T, std::string>) {
                std::string obj(data.begin(), data.end());
                handler(obj);
            } else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                std::vector<std::uint8_t> obj(data.begin(), data.end());
                handler(obj);
            } else if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) > 0) {
                if (data.size() >= sizeof(T)) {
                    T obj;
                    std::memcpy(&obj, data.data(), sizeof(T));
                    handler(obj);
                }
            } else {
                handler(data);
            }
        };
    }

private:
    template<typename T>
    constexpr std::uint32_t get_type_id() const {
        return TypeRegistry::get_type_id<T>();
    }

    template<typename Handler>
    void dispatch_by_type_id(std::uint32_t type_id, traits::buffer_view<const std::uint8_t> payload, Handler&& handler) {
        auto it = handlers_.find(type_id);
        if (it != handlers_.end()) {
            MDEBUG("Found registered handler for type_id: {}", type_id);
            it->second(payload);
        } else {
            MDEBUG("No handler found for type_id: {}, using fallback", type_id);
            handler(std::vector<std::uint8_t>(payload.begin(), payload.end()));
        }
    }

    std::unordered_map<std::uint32_t, std::function<void(traits::buffer_view<const std::uint8_t>)>> handlers_;
};

class JsonMessage : public RawMessageBase<JsonMessage> {
public:
    JsonMessage() {
        MINFO("JsonMessage initialized with pretty_print={}, indent={}", pretty_print_, indent_spaces_);
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_impl(const T& obj) {
        MTRACE("JsonMessage encoding type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));

        try {
            nlohmann::json j;

            if constexpr (std::is_same_v<T, std::string>) {
                j = obj;
                MDEBUG("Encoded string to JSON");
            } else if constexpr (std::is_arithmetic_v<T>) {
                j = obj;
                MDEBUG("Encoded arithmetic type to JSON");
            } else if constexpr (std::is_same_v<T, bool>) {
                j = obj;
                MDEBUG("Encoded bool to JSON");
            } else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                j = nlohmann::json::array();
                for (const auto& byte : obj) {
                    j.push_back(static_cast<int>(byte));
                }
                MDEBUG("Encoded vector<uint8_t> to JSON array with {} elements", obj.size());
            } else if constexpr (std::is_same_v<T, std::vector<typename T::value_type>>) {
                j = nlohmann::json::array();
                for (const auto& item : obj) {
                    j.push_back(item);
                }
                MDEBUG("Encoded vector to JSON array with {} elements", obj.size());
            } else if constexpr (std::is_same_v<T, std::unordered_map<std::string, typename T::mapped_type>>) {
                j = nlohmann::json::object();
                for (const auto& [key, value] : obj) {
                    j[key] = value;
                }
                MDEBUG("Encoded unordered_map to JSON object with {} keys", obj.size());
            } else {
                j = obj;
                MDEBUG("Encoded custom type to JSON");
            }

            std::string json_str = pretty_print_ ? j.dump(indent_spaces_) : j.dump();
            ++stats_.messages_encoded;
            stats_.bytes_encoded += json_str.size();

            MDEBUG("JSON encoding successful: {} bytes", json_str.size());
            return std::vector<std::uint8_t>(json_str.begin(), json_str.end());

        } catch (const std::exception& e) {
            ++stats_.encode_errors;
            MERROR("JSON encoding failed: {}", e.what());
            std::string error_json = "{\"error\":\"" + std::string(e.what()) + "\"}";
            return std::vector<std::uint8_t>(error_json.begin(), error_json.end());
        }
    }

    template<typename Handler>
    void dispatch_impl(traits::buffer_view<const std::uint8_t> data, Handler&& handler) {
        MTRACE("JsonMessage dispatching {} bytes", data.size());

        try {
            std::string json_str(data.begin(), data.end());
            MDEBUG("Parsing JSON string: {} characters", json_str.size());

            nlohmann::json j = nlohmann::json::parse(json_str);

            ++stats_.messages_decoded;
            stats_.bytes_decoded += data.size();

            MDEBUG("JSON parsing successful");
            handler(j);

        } catch (const std::exception& e) {
            ++stats_.parse_errors;
            MERROR("JSON parsing failed: {}", e.what());
            std::string json_str(data.begin(), data.end());
            handler(json_str);
        }
    }

    template<typename T, typename Handler>
    void register_json_handler(const std::string& type_field, const std::string& type_value, Handler&& handler) {
        MINFO("Registering JSON handler: field='{}', value='{}'", type_field, type_value);

        json_handlers_[type_value] = [handler = std::forward<Handler>(handler), type_field, type_value](const nlohmann::json& j) {
            try {
                if (j.contains(type_field) && j[type_field] == type_value) {
                    T obj = j.get<T>();
                    handler(obj);
                }
            } catch (const std::exception& e) {
                // Log error in actual implementation
            }
        };
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_with_type(const T& obj, const std::string& type_name) {
        MTRACE("Encoding with type annotation: {}", type_name);

        try {
            nlohmann::json j;
            j["type"] = type_name;
            j["data"] = obj;

            std::string json_str = pretty_print_ ? j.dump(indent_spaces_) : j.dump();
            MDEBUG("Encoded with type annotation: {} bytes", json_str.size());
            return std::vector<std::uint8_t>(json_str.begin(), json_str.end());

        } catch (const std::exception& e) {
            MERROR("Failed to encode with type: {}", e.what());
            std::string error_json = "{\"error\":\"" + std::string(e.what()) + "\"}";
            return std::vector<std::uint8_t>(error_json.begin(), error_json.end());
        }
    }

    void set_pretty_print(bool enable) {
        MINFO("Setting pretty print: {}", enable);
        pretty_print_ = enable;
    }

    void set_indent(int spaces) {
        MINFO("Setting indent spaces: {}", spaces);
        indent_spaces_ = spaces;
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_compact(const T& obj) {
        MTRACE("Encoding in compact format");

        try {
            nlohmann::json j = obj;
            std::string json_str = j.dump(-1);
            MDEBUG("Compact encoding successful: {} bytes", json_str.size());
            return std::vector<std::uint8_t>(json_str.begin(), json_str.end());
        } catch (const std::exception& e) {
            MERROR("Compact encoding failed: {}", e.what());
            std::string error_json = "{\"error\":\"" + std::string(e.what()) + "\"}";
            return std::vector<std::uint8_t>(error_json.begin(), error_json.end());
        }
    }

    bool is_valid_json(traits::buffer_view<const std::uint8_t> data) const {
        try {
            std::string json_str(data.begin(), data.end());
            auto temp = nlohmann::json::parse(json_str);
            MDEBUG("JSON validation successful");
            return true;
        } catch (const std::exception& e) {
            MDEBUG("JSON validation failed: {}", e.what());
            return false;
        }
    }

    struct Stats {
        size_t messages_encoded = 0;
        size_t messages_decoded = 0;
        size_t parse_errors = 0;
        size_t encode_errors = 0;
        size_t bytes_encoded = 0;
        size_t bytes_decoded = 0;
    };

    Stats get_stats() const noexcept {
        MTRACE("Getting JSON stats: encoded={}, decoded={}, parse_errors={}, encode_errors={}",
               stats_.messages_encoded, stats_.messages_decoded, stats_.parse_errors, stats_.encode_errors);
        return stats_;
    }

    void reset_stats() noexcept {
        MINFO("Resetting JSON statistics");
        stats_ = Stats{};
    }

private:
    std::unordered_map<std::string, std::function<void(const nlohmann::json&)>> json_handlers_;
    mutable Stats stats_;
    bool pretty_print_ = false;
    int indent_spaces_ = 2;
};

template<typename BaseMessage>
class Compressed : public RawMessageBase<Compressed<BaseMessage>> {
public:
    template<typename... Args>
    explicit Compressed(Args&&... args) : base_(std::forward<Args>(args)...) {
        MINFO("Compressed wrapper initialized");
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_impl(const T& obj) {
        MTRACE("Compressed encoding type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));

        auto encoded = base_.template encode<T>(obj);
        if (!encoded) {
            MWARN("Base encoding failed");
            return std::nullopt;
        }

        std::vector<std::uint8_t> result;
        std::uint8_t compression_flag = 1;
        result.push_back(compression_flag);
        result.insert(result.end(), encoded->begin(), encoded->end());

        MDEBUG("Compression complete: {} -> {} bytes", encoded->size(), result.size());
        return result;
    }

    template<typename Handler>
    void dispatch_impl(traits::buffer_view<const std::uint8_t> data, Handler&& handler) {
        MTRACE("Compressed dispatching {} bytes", data.size());

        if (data.empty()) {
            MWARN("Empty data for decompression");
            return;
        }

        std::uint8_t compression_flag = data[0];
        if (compression_flag == 1) {
            MDEBUG("Decompressing data");
            traits::buffer_view<const std::uint8_t> decompressed_data(
                data.data() + 1, data.size() - 1);
            base_.dispatch(decompressed_data, std::forward<Handler>(handler));
        } else {
            MDEBUG("Data not compressed, passing through");
            base_.dispatch(data, std::forward<Handler>(handler));
        }
    }

    BaseMessage& base() noexcept { return base_; }
    const BaseMessage& base() const noexcept { return base_; }

private:
    BaseMessage base_;
};

template<typename BaseMessage>
class Encrypted : public RawMessageBase<Encrypted<BaseMessage>> {
public:
    template<typename... Args>
    explicit Encrypted(Args&&... args) : base_(std::forward<Args>(args)...) {
        MINFO("Encrypted wrapper initialized");
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_impl(const T& obj) {
        MTRACE("Encrypted encoding type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));

        auto encoded = base_.template encode<T>(obj);
        if (!encoded) {
            MWARN("Base encoding failed");
            return std::nullopt;
        }

        std::vector<std::uint8_t> result = *encoded;
        std::uint8_t key = 0xAA;

        for (auto& byte : result) {
            byte ^= key;
        }

        MDEBUG("Encryption complete: {} bytes", result.size());
        return result;
    }

    template<typename Handler>
    void dispatch_impl(traits::buffer_view<const std::uint8_t> data, Handler&& handler) {
        MTRACE("Encrypted dispatching {} bytes", data.size());

        std::vector<std::uint8_t> decrypted(data.begin(), data.end());
        std::uint8_t key = 0xAA;

        for (auto& byte : decrypted) {
            byte ^= key;
        }

        MDEBUG("Decryption complete");
        traits::buffer_view<const std::uint8_t> decrypted_view(
            decrypted.data(), decrypted.size());
        base_.dispatch(decrypted_view, std::forward<Handler>(handler));
    }

    BaseMessage& base() noexcept { return base_; }
    const BaseMessage& base() const noexcept { return base_; }

private:
    BaseMessage base_;
};

template<typename MessageType>
class MessageRegistry : public logger::LogAccessor<MessageRegistry<MessageType>> {
public:
    MessageRegistry() {
        MINFO("MessageRegistry initialized");
    }

    template<typename T, typename Handler>
    void register_handler(Handler&& handler) {
        std::uint32_t type_id = TypeRegistry::get_type_id<T>();
        MINFO("Registering handler for type: {}, id: {}", TypeRegistry::get_type_name(type_id), type_id);

        handlers_[type_id] = [handler = std::forward<Handler>(handler)](traits::buffer_view<const std::uint8_t> data) {
            if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) > 0) {
                if (data.size() >= sizeof(T)) {
                    T obj;
                    std::memcpy(&obj, data.data(), sizeof(T));
                    handler(obj);
                }
            }
        };
    }

    template<typename Handler>
    void dispatch(std::uint32_t type_id, traits::buffer_view<const std::uint8_t> data, Handler&& fallback) {
        MTRACE("Dispatching message: type_id={}, size={}", type_id, data.size());

        auto it = handlers_.find(type_id);
        if (it != handlers_.end()) {
            MDEBUG("Found handler for type_id: {}", type_id);
            it->second(data);
        } else {
            MDEBUG("No handler found for type_id: {}, using fallback", type_id);
            fallback(data);
        }
    }

    void clear() {
        MINFO("Clearing all handlers");
        handlers_.clear();
    }

    std::size_t size() const {
        MTRACE("Registry size: {}", handlers_.size());
        return handlers_.size();
    }

private:
    template<typename T>
    constexpr std::uint32_t get_type_id() const {
        return TypeRegistry::get_type_id<T>();
    }

    std::unordered_map<std::uint32_t, std::function<void(traits::buffer_view<const std::uint8_t>)>> handlers_;
};

template<typename BaseMessage>
class BatchMessage : public RawMessageBase<BatchMessage<BaseMessage>> {
public:
    template<typename... Args>
    explicit BatchMessage(Args&&... args) : base_(std::forward<Args>(args)...) {
        MINFO("BatchMessage initialized");
    }

    template<typename T>
    std::optional<std::vector<std::uint8_t>> encode_impl(const T& obj) {
        MTRACE("BatchMessage encoding type: {}", TypeRegistry::get_type_name(TypeRegistry::get_type_id<T>()));

        if constexpr (std::is_same_v<T, std::vector<typename T::value_type>>) {
            std::vector<std::uint8_t> result;

            std::uint32_t count = static_cast<std::uint32_t>(obj.size());
            MDEBUG("Encoding batch with {} items", count);

            result.resize(sizeof(count));
            std::memcpy(result.data(), &count, sizeof(count));

            for (const auto& item : obj) {
                auto encoded = base_.template encode(item);
                if (!encoded) {
                    MWARN("Failed to encode batch item");
                    return std::nullopt;
                }

                std::uint32_t msg_len = static_cast<std::uint32_t>(encoded->size());
                size_t old_size = result.size();
                result.resize(old_size + sizeof(msg_len) + encoded->size());
                std::memcpy(result.data() + old_size, &msg_len, sizeof(msg_len));
                std::memcpy(result.data() + old_size + sizeof(msg_len),
                           encoded->data(), encoded->size());
            }

            MDEBUG("Batch encoding complete: {} total bytes", result.size());
            return result;
        } else {
            return base_.template encode<T>(obj);
        }
    }

    template<typename Handler>
    void dispatch_impl(traits::buffer_view<const std::uint8_t> data, Handler&& handler) {
        MTRACE("BatchMessage dispatching {} bytes", data.size());

        if (data.size() < sizeof(std::uint32_t)) {
            MDEBUG("Not a batch message, delegating to base");
            base_.dispatch(data, std::forward<Handler>(handler));
            return;
        }

        std::uint32_t count;
        std::memcpy(&count, data.data(), sizeof(count));

        if (count == 0 || count > 1000) {
            MWARN("Invalid batch count: {}, delegating to base", count);
            base_.dispatch(data, std::forward<Handler>(handler));
            return;
        }

        MDEBUG("Processing batch with {} messages", count);

        size_t offset = sizeof(count);
        for (std::uint32_t i = 0; i < count && offset < data.size(); ++i) {
            if (offset + sizeof(std::uint32_t) > data.size()) {
                MWARN("Incomplete batch message at item {}", i);
                break;
            }

            std::uint32_t msg_len;
            std::memcpy(&msg_len, data.data() + offset, sizeof(msg_len));
            offset += sizeof(msg_len);

            if (offset + msg_len > data.size()) {
                MWARN("Message length mismatch at item {}: expected {}, available {}", i, msg_len, data.size() - offset);
                break;
            }

            traits::buffer_view<const std::uint8_t> msg_data(
                data.data() + offset, msg_len);
            base_.dispatch(msg_data, handler);

            offset += msg_len;
        }

        MDEBUG("Batch processing complete");
    }

    BaseMessage& base() noexcept { return base_; }
    const BaseMessage& base() const noexcept { return base_; }

private:
    BaseMessage base_;
};

} // namespace comm::message

namespace comm::traits {

template<>
struct is_realtime_capable<comm::message::RawBytes> : std::true_type {};

template<typename T>
struct supports_compression<comm::message::Compressed<T>> : std::true_type {};

template<typename T>
struct supports_encryption<comm::message::Encrypted<T>> : std::true_type {};

template<>
struct memory_model<comm::message::RawBytes> {
    static constexpr bool is_static = true;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_pool_based = false;
};

template<typename T>
struct memory_model<comm::message::Compressed<T>> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = false;
};

template<typename T>
struct memory_model<comm::message::Encrypted<T>> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = false;
};

} // namespace comm::traits

#endif // COMM_MESSAGE_RAW_HPP