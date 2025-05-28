#pragma once

#ifndef COMM_CORE_ENDPOINT_HPP
#define COMM_CORE_ENDPOINT_HPP

#include <cstdint>
#include <string>
#include <optional>
#include <functional>

namespace comm::core {

enum class ChannelState : std::uint8_t {
    CLOSED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    DISCONNECTING = 3,
    ERROR = 4
};

enum class MessageType : std::uint32_t {
    DATA = 0x00000000,
    ACK = 0x00000001,
    NACK = 0x00000002,
    HEARTBEAT = 0x00000003,
    HELLO = 0x00000004,
    GOODBYE = 0x00000005,

    RPC_REQUEST = 0x00001000,
    RPC_RESPONSE = 0x00001001,
    RPC_ERROR = 0x00001002,

    PUBLISH = 0x00002000,
    SUBSCRIBE = 0x00002001,
    UNSUBSCRIBE = 0x00002002,

    SYSTEM_INFO = 0x00003000,
    SYSTEM_ERROR = 0x00003001,

    USER_DEFINED = 0x10000000
};

struct EndpointID {
    uint32_t node_id;
    uint32_t proc_id;
    uint32_t port_id;
    uint32_t reserved;

    constexpr EndpointID() noexcept
        : node_id(0), proc_id(0), port_id(0), reserved(0) {}

    constexpr EndpointID(std::uint32_t node, std::uint32_t proc,
                        std::uint32_t port, std::uint32_t res = 0) noexcept
        : node_id(node), proc_id(proc), port_id(port), reserved(res) {}

    explicit constexpr EndpointID(std::uint64_t simple_id) noexcept
        : node_id(static_cast<std::uint32_t>(simple_id >> 32))
        , proc_id(static_cast<std::uint32_t>(simple_id & 0xFFFFFFFF))
        , port_id(0), reserved(0) {}

    constexpr std::uint64_t to_simple() const noexcept {
        return (static_cast<std::uint64_t>(node_id) << 32) | proc_id;
    }

    constexpr bool operator==(const EndpointID& other) const noexcept {
        return node_id == other.node_id &&
               proc_id == other.proc_id &&
               port_id == other.port_id &&
               reserved == other.reserved;
    }

    constexpr bool operator!=(const EndpointID& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator<(const EndpointID& other) const noexcept {
        if (node_id != other.node_id) return node_id < other.node_id;
        if (proc_id != other.proc_id) return proc_id < other.proc_id;
        if (port_id != other.port_id) return port_id < other.port_id;
        return reserved < other.reserved;
    }

    constexpr bool is_broadcast() const noexcept {
        return node_id == 0xFFFFFFFF;
    }

    constexpr bool is_local() const noexcept {
        // 更准确的本地判断：0表示本机，0x7F000001是127.0.0.1
        return node_id == 0 || node_id == 0x7F000001 ||
               (node_id >= 0x7F000000 && node_id <= 0x7FFFFFFF); // 127.x.x.x范围
    }

    std::string to_string() const {
        return std::to_string(node_id) + ":" +
               std::to_string(proc_id) + ":" +
               std::to_string(port_id) + ":" +
               std::to_string(reserved);
    }

    static std::optional<EndpointID> from_string(std::string_view str) {
        // 简单实现，实际项目中应该使用更健壮的解析
        // 这里提供基本实现避免链接错误
        return std::nullopt; // TODO: 实现字符串解析
    }
};

namespace endpoints {
    constexpr EndpointID INVALID{0, 0, 0, 0};
    constexpr EndpointID BROADCAST{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0};
    constexpr EndpointID LOCAL_ANY{0, 0, 0, 0};
} // namespace endpoints

struct URI {
    std::string scheme;
    std::string host;
    std::uint16_t port;
    std::string path;
    std::string query;
    std::string fragment;

    URI() : port(0) {}

    static std::optional<URI> parse(std::string_view uri_str);

    std::string to_string() const;

    static URI shm(std::string_view key, std::uint16_t port = 0);
    static URI tcp(std::string_view host, std::uint16_t port);
    static URI udp(std::string_view host, std::uint16_t port);
    static URI uart(std::string_view device, std::uint32_t baudrate = 115200);
};

struct ChannelConfig {
    URI uri;
    EndpointID local_endpoint;
    EndpointID remote_endpoint;

    enum class Priority : std::uint8_t {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };

    Priority priority = Priority::NORMAL;
    std::uint32_t timeout_ms = 5000;
    std::uint16_t max_retries = 3;
    std::uint16_t mtu = 1500;

    bool enable_compression = false;
    bool enable_encryption = false;
    bool enable_zero_copy = false;
    bool enable_reliable = true;
    bool enable_ordered = true;
};

} // namespace comm::core

// 将hash特化放在std命名空间内，符合C++标准
namespace std {
    template<>
    struct hash<comm::core::EndpointID> {
        std::size_t operator()(const comm::core::EndpointID& ep) const noexcept {
            // 使用更好的hash组合算法，避免碰撞
            std::size_t h1 = std::hash<std::uint32_t>{}(ep.node_id);
            std::size_t h2 = std::hash<std::uint32_t>{}(ep.proc_id);
            std::size_t h3 = std::hash<std::uint32_t>{}(ep.port_id);
            std::size_t h4 = std::hash<std::uint32_t>{}(ep.reserved);

            // 使用更好的hash组合算法（类似boost::hash_combine）
            std::size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h4 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}

#endif // COMM_CORE_ENDPOINT_HPP