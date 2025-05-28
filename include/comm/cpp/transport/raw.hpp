#pragma once

#ifndef COMM_TRANSPORT_RAW_HPP
#define COMM_TRANSPORT_RAW_HPP

#include "../core/traits.hpp"
#include <optional>
#include <vector>
#include <cstdint>
#include <chrono>
#include <cstring>

namespace comm::transport {

template<typename Derived>
class RawTransportBase {
public:
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }
    const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

    std::optional<std::vector<std::uint8_t>> wrap(traits::buffer_view<const std::uint8_t> data) {
        return derived().wrap_impl(data);
    }

    std::optional<std::vector<std::uint8_t>> unwrap(traits::buffer_view<const std::uint8_t> data) {
        return derived().unwrap_impl(data);
    }

protected:
    RawTransportBase() = default;
    ~RawTransportBase() = default;
};

class PassThrough : public RawTransportBase<PassThrough> {
public:
    std::optional<std::vector<std::uint8_t>> wrap_impl(traits::buffer_view<const std::uint8_t> data) {
        return std::vector<std::uint8_t>(data.begin(), data.end());
    }

    std::optional<std::vector<std::uint8_t>> unwrap_impl(traits::buffer_view<const std::uint8_t> data) {
        return std::vector<std::uint8_t>(data.begin(), data.end());
    }
};

class CrcTransport : public RawTransportBase<CrcTransport> {
public:
    std::optional<std::vector<std::uint8_t>> wrap_impl(traits::buffer_view<const std::uint8_t> data) {
        std::vector<std::uint8_t> result(data.begin(), data.end());

        std::uint32_t crc = calculate_crc32(data);

        result.resize(result.size() + sizeof(crc));
        std::memcpy(result.data() + data.size(), &crc, sizeof(crc));

        return result;
    }

    std::optional<std::vector<std::uint8_t>> unwrap_impl(traits::buffer_view<const std::uint8_t> data) {
        if (data.size() < sizeof(std::uint32_t)) {
            return std::nullopt;
        }

        std::size_t payload_size = data.size() - sizeof(std::uint32_t);
        traits::buffer_view<const std::uint8_t> payload(data.data(), payload_size);

        std::uint32_t received_crc;
        std::memcpy(&received_crc, data.data() + payload_size, sizeof(received_crc));

        std::uint32_t calculated_crc = calculate_crc32(payload);
        if (received_crc != calculated_crc) {
            return std::nullopt;
        }

        return std::vector<std::uint8_t>(payload.begin(), payload.end());
    }

private:
    // 改进的CRC32实现，使用查表法提高性能
    std::uint32_t calculate_crc32(traits::buffer_view<const std::uint8_t> data) const {
        static constexpr std::uint32_t crc_table[256] = {
            0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
            0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
            0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
            0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
            0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
            0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
            0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
            0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
            0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
            0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
            0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
            0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
            0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
            0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
            0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
            0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
            0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
            0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
            0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
            0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
            0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
            0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
            0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
            0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
            0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
            0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
            0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
            0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
            0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
            0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
            0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
            0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
            0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
            0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
            0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
            0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
            0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
            0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
            0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
            0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
            0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
            0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
            0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
        };

        std::uint32_t crc = 0xFFFFFFFF;
        for (auto byte : data) {
            crc = crc_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }
};

// 带长度前缀的传输
class LengthPrefixed : public RawTransportBase<LengthPrefixed> {
public:
    std::optional<std::vector<uint8_t>> wrap_impl(traits::buffer_view<const std::uint8_t> data) {
        std::vector<uint8_t> result;

        // 添加长度前缀
        std::uint32_t length = static_cast<std::uint32_t>(data.size());
        result.resize(sizeof(length));
        std::memcpy(result.data(), &length, sizeof(length));

        // 添加数据
        result.insert(result.end(), data.begin(), data.end());

        return result;
    }

    std::optional<std::vector<uint8_t>> unwrap_impl(traits::buffer_view<const std::uint8_t> data) {
        if (data.size() < sizeof(std::uint32_t)) {
            return std::nullopt;
        }

        std::uint32_t length;
        std::memcpy(&length, data.data(), sizeof(length));

        // 使用与C层一致的最大长度限制
        if (length > COMM_CFG_MAX_FRAME_SIZE) {
            return std::nullopt;
        }

        if (data.size() < sizeof(length) + length) {
            return std::nullopt;
        }

        return std::vector<uint8_t>(
            data.begin() + sizeof(length),
            data.begin() + sizeof(length) + length);
    }
};

class Timestamped : public RawTransportBase<Timestamped> {
public:
    std::optional<std::vector<std::uint8_t>> wrap_impl(traits::buffer_view<const std::uint8_t> data) {
        std::vector<std::uint8_t> result;

        std::uint64_t timestamp = get_timestamp();
        result.resize(sizeof(timestamp));
        std::memcpy(result.data(), &timestamp, sizeof(timestamp));

        result.insert(result.end(), data.begin(), data.end());

        return result;
    }

    std::optional<std::vector<std::uint8_t>> unwrap_impl(traits::buffer_view<const std::uint8_t> data) {
        if (data.size() < sizeof(std::uint64_t)) {
            return std::nullopt;
        }

        std::uint64_t timestamp;
        std::memcpy(&timestamp, data.data(), sizeof(timestamp));

        last_received_timestamp_ = timestamp;

        return std::vector<std::uint8_t>(
            data.begin() + sizeof(timestamp),
            data.end());
    }

    std::uint64_t get_last_timestamp() const noexcept {
        return last_received_timestamp_;
    }

private:
    std::uint64_t get_timestamp() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    std::uint64_t last_received_timestamp_ = 0;
};

// 复合传输层 - 支持多个传输策略的组合
// 包装顺序：First -> Rest (First是最内层，Rest是外层)
// 例如：CompositeTransport<Length, CRC> 会产生 CRC(Length(data))
// 解包顺序：Rest -> First (先解外层，再解内层)
template<typename... Transports>
class CompositeTransport;

template<typename Transport>
class CompositeTransport<Transport> : public RawTransportBase<CompositeTransport<Transport>> {
public:
    template<typename... Args>
    explicit CompositeTransport(Args&&... args) : transport_(std::forward<Args>(args)...) {}

    std::optional<std::vector<std::uint8_t>> wrap_impl(traits::buffer_view<const std::uint8_t> data) {
        return transport_.wrap(data);
    }

    std::optional<std::vector<std::uint8_t>> unwrap_impl(traits::buffer_view<const std::uint8_t> data) {
        return transport_.unwrap(data);
    }

    Transport& get() noexcept { return transport_; }
    const Transport& get() const noexcept { return transport_; }

private:
    Transport transport_;
};

template<typename First, typename... Rest>
class CompositeTransport<First, Rest...> : public RawTransportBase<CompositeTransport<First, Rest...>> {
public:
    template<typename... Args>
    explicit CompositeTransport(Args&&... args)
        : first_(std::forward<Args>(args)...), rest_(std::forward<Args>(args)...) {}

    std::optional<std::vector<std::uint8_t>> wrap_impl(traits::buffer_view<const std::uint8_t> data) {
        auto wrapped = first_.wrap(data);
        if (!wrapped) {
            return std::nullopt;
        }

        traits::buffer_view<const std::uint8_t> wrapped_view(wrapped->data(), wrapped->size());
        return rest_.wrap(wrapped_view);
    }

    std::optional<std::vector<std::uint8_t>> unwrap_impl(traits::buffer_view<const std::uint8_t> data) {
        auto unwrapped = rest_.unwrap(data);
        if (!unwrapped) {
            return std::nullopt;
        }

        traits::buffer_view<const std::uint8_t> unwrapped_view(unwrapped->data(), unwrapped->size());
        return first_.unwrap(unwrapped_view);
    }

    First& first() noexcept { return first_; }
    const First& first() const noexcept { return first_; }

    CompositeTransport<Rest...>& rest() noexcept { return rest_; }
    const CompositeTransport<Rest...>& rest() const noexcept { return rest_; }

private:
    First first_;
    CompositeTransport<Rest...> rest_;
};

using CrcLengthPrefixed = CompositeTransport<LengthPrefixed, CrcTransport>;
using TimestampedCrc = CompositeTransport<Timestamped, CrcTransport>;
using FullTransport = CompositeTransport<LengthPrefixed, Timestamped, CrcTransport>;

} // namespace comm::transport

namespace comm::traits {

    template<>
    struct is_realtime_capable<comm::transport::PassThrough> : std::true_type {};

    template<>
    struct memory_model<comm::transport::PassThrough> {
        static constexpr bool is_static = true;
        static constexpr bool is_dynamic = false;
        static constexpr bool is_pool_based = false;
    };

    template<>
    struct memory_model<comm::transport::CrcTransport> {
        static constexpr bool is_static = false;
        static constexpr bool is_dynamic = true;
        static constexpr bool is_pool_based = false;
    };

    template<>
    struct memory_model<comm::transport::LengthPrefixed> {
        static constexpr bool is_static = false;
        static constexpr bool is_dynamic = true;
        static constexpr bool is_pool_based = false;
    };

    template<>
    struct memory_model<comm::transport::Timestamped> {
        static constexpr bool is_static = false;
        static constexpr bool is_dynamic = true;
        static constexpr bool is_pool_based = false;
    };

    template<typename... Transports>
    struct memory_model<comm::transport::CompositeTransport<Transports...>> {
        static constexpr bool is_static = false;
        static constexpr bool is_dynamic = true;
        static constexpr bool is_pool_based = false;
    };

} // namespace comm::traits

#endif // COMM_TRANSPORT_RAW_HPP