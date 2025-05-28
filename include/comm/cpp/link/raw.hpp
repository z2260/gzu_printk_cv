#pragma once

#ifndef COMM_LINK_RAW_HPP
#define COMM_LINK_RAW_HPP

#include "../core/traits.hpp"
#include <optional>
#include <vector>
#include <cstdint>
#include <deque>
#include <limits>

namespace comm::link {

template<typename Derived>
class RawLinkBase {
public:
    Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    const Derived& derived() const noexcept {
        return static_cast<const Derived&>(*this);
    }

    std::size_t mtu() const noexcept {
        return derived().mtu_impl();
    }

    bool write(std::uint32_t endpoint, traits::buffer_view<const std::uint8_t> data) {
        return derived().write_impl(endpoint, data);
    }

    std::optional<std::vector<std::uint8_t>> read() {
        return derived().read_impl();
    }

    bool is_connected() const noexcept {
        if constexpr (traits::has_is_connected_v<Derived>) {
            return derived().is_connected_impl();
        } else {
            return true;
        }
    }

    void close() noexcept {
        if constexpr (traits::has_close_v<Derived>) {
            derived().close_impl();
        }
    }

    template<typename StatsType = void>
    auto get_stats() const {
        if constexpr (traits::has_get_stats_v<Derived>) {
            return derived().get_stats_impl();
        } else {
            struct EmptyStats {
                std::size_t bytes_sent = 0;
                std::size_t bytes_received = 0;
                std::size_t packets_sent = 0;
                std::size_t packets_received = 0;
            };
            return EmptyStats{};
        }
    }

protected:
    RawLinkBase() = default;
    ~RawLinkBase() = default;

    RawLinkBase(const RawLinkBase&) = delete;
    RawLinkBase& operator=(const RawLinkBase&) = delete;
    RawLinkBase(RawLinkBase&&) = delete;
    RawLinkBase& operator=(RawLinkBase&&) = delete;
};

template<std::size_t BufferSize = 4096>
class MemoryLink : public RawLinkBase<MemoryLink<BufferSize>> {
public:
    MemoryLink() = default;

    std::size_t mtu_impl() const noexcept {
        return BufferSize;
    }

    bool write_impl(std::uint32_t endpoint, traits::buffer_view<const std::uint8_t> data) {
        if (data.size() > BufferSize) {
            return false;
        }

        buffer_.resize(data.size());
        std::copy_n(data.data(), data.size(), buffer_.data());
        has_data_ = true;
        endpoint_ = endpoint;

        ++stats_.packets_sent;
        stats_.bytes_sent += data.size();

        return true;
    }

    std::optional<std::vector<std::uint8_t>> read_impl() {
        if (!has_data_) {
            return std::nullopt;
        }

        has_data_ = false;

        ++stats_.packets_received;
        stats_.bytes_received += buffer_.size();

        return std::move(buffer_);
    }

    bool is_connected_impl() const noexcept { return true; }

    void close_impl() noexcept {
        has_data_ = false;
        buffer_.clear();
    }

    struct Stats {
        std::size_t bytes_sent = 0;
        std::size_t bytes_received = 0;
        std::size_t packets_sent = 0;
        std::size_t packets_received = 0;
    };

    Stats get_stats_impl() const noexcept { return stats_; }

    void reset_stats() noexcept { stats_ = Stats{}; }

    bool has_pending_data() const noexcept { return has_data_; }
    std::uint32_t last_endpoint() const noexcept { return endpoint_; }

private:
    std::vector<std::uint8_t> buffer_;
    bool has_data_ = false;
    std::uint32_t endpoint_ = 0;
    Stats stats_;
};

class NullLink : public RawLinkBase<NullLink> {
public:
    std::size_t mtu_impl() const noexcept {
        return std::numeric_limits<std::size_t>::max();
    }

    bool write_impl(std::uint32_t, traits::buffer_view<const std::uint8_t>) {
        return true;
    }

    std::optional<std::vector<std::uint8_t>> read_impl() {
        return std::nullopt;
    }

    bool is_connected_impl() const noexcept { return true; }
    void close_impl() noexcept {}
};

template<std::size_t QueueSize = 16>
class LoopbackLink : public RawLinkBase<LoopbackLink<QueueSize>> {
public:
    LoopbackLink() = default;

    std::size_t mtu_impl() const noexcept {
        return 65536;
    }

    bool write_impl(std::uint32_t endpoint, traits::buffer_view<const std::uint8_t> data) {
        if (queue_.size() >= QueueSize) {
            ++stats_.queue_overflows;
            return false;
        }

        Packet packet;
        packet.endpoint = endpoint;
        packet.data.assign(data.begin(), data.end());

        queue_.push_back(std::move(packet));

        ++stats_.packets_sent;
        stats_.bytes_sent += data.size();

        return true;
    }

    std::optional<std::vector<std::uint8_t>> read_impl() {
        if (queue_.empty()) {
            return std::nullopt;
        }

        auto packet = std::move(queue_.front());
        queue_.pop_front();

        ++stats_.packets_received;
        stats_.bytes_received += packet.data.size();

        return std::move(packet.data);
    }

    bool is_connected_impl() const noexcept { return true; }

    void close_impl() noexcept {
        queue_.clear();
    }

    struct Stats {
        std::size_t bytes_sent = 0;
        std::size_t bytes_received = 0;
        std::size_t packets_sent = 0;
        std::size_t packets_received = 0;
        std::size_t queue_overflows = 0;
    };

    Stats get_stats_impl() const noexcept { return stats_; }

    void reset_stats() noexcept { stats_ = Stats{}; }

    std::size_t queue_size() const noexcept { return queue_.size(); }
    bool is_queue_full() const noexcept { return queue_.size() >= QueueSize; }
    bool is_queue_empty() const noexcept { return queue_.empty(); }

    void clear_queue() noexcept { queue_.clear(); }

    template<typename Iterator>
    std::size_t write_batch(std::uint32_t endpoint, Iterator begin, Iterator end) {
        std::size_t written = 0;
        for (auto it = begin; it != end && queue_.size() < QueueSize; ++it) {
            if (write_impl(endpoint, *it)) {
                ++written;
            } else {
                break;
            }
        }
        return written;
    }

private:
    struct Packet {
        std::uint32_t endpoint;
        std::vector<std::uint8_t> data;
    };

    std::deque<Packet> queue_;
    Stats stats_;
};

template<std::size_t SendBufferSize = 8192, std::size_t RecvBufferSize = 8192>
class BufferedLink : public RawLinkBase<BufferedLink<SendBufferSize, RecvBufferSize>> {
public:
    BufferedLink() = default;

    std::size_t mtu_impl() const noexcept {
        return std::min(SendBufferSize, RecvBufferSize) / 2;
    }

    bool write_impl(std::uint32_t endpoint, traits::buffer_view<const std::uint8_t> data) {
        if (data.size() > SendBufferSize - send_buffer_.size()) {
            return false;
        }

        send_buffer_.insert(send_buffer_.end(), data.begin(), data.end());

        flush_to_receive();

        return true;
    }

    std::optional<std::vector<std::uint8_t>> read_impl() {
        if (recv_buffer_.empty()) {
            return std::nullopt;
        }

        std::vector<std::uint8_t> result = std::move(recv_buffer_);
        recv_buffer_.clear();

        return result;
    }

    bool is_connected_impl() const noexcept { return true; }

    void close_impl() noexcept {
        send_buffer_.clear();
        recv_buffer_.clear();
    }

    void flush() {
        flush_to_receive();
    }

    std::size_t send_buffer_size() const noexcept { return send_buffer_.size(); }
    std::size_t recv_buffer_size() const noexcept { return recv_buffer_.size(); }
    std::size_t send_buffer_available() const noexcept {
        return SendBufferSize - send_buffer_.size();
    }
    std::size_t recv_buffer_available() const noexcept {
        return RecvBufferSize - recv_buffer_.size();
    }

private:
    void flush_to_receive() {
        if (!send_buffer_.empty() &&
            recv_buffer_.size() + send_buffer_.size() <= RecvBufferSize) {

            recv_buffer_.insert(recv_buffer_.end(),
                              send_buffer_.begin(), send_buffer_.end());
            send_buffer_.clear();
        }
    }

    std::vector<std::uint8_t> send_buffer_;
    std::vector<std::uint8_t> recv_buffer_;
};

} // namespace comm::link

namespace comm::traits {

template<std::size_t BufferSize>
struct supports_zero_copy<comm::link::MemoryLink<BufferSize>> : std::true_type {};

template<>
struct is_realtime_capable<comm::link::NullLink> : std::true_type {};

template<std::size_t QueueSize>
struct is_realtime_capable<comm::link::LoopbackLink<QueueSize>> : std::true_type {};

template<std::size_t BufferSize>
struct memory_model<comm::link::MemoryLink<BufferSize>> {
    static constexpr bool is_static = true;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_pool_based = false;
};

template<std::size_t QueueSize>
struct memory_model<comm::link::LoopbackLink<QueueSize>> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = false;
};

template<std::size_t SendBufferSize, std::size_t RecvBufferSize>
struct memory_model<comm::link::BufferedLink<SendBufferSize, RecvBufferSize>> {
    static constexpr bool is_static = true;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_pool_based = true;
};

template<std::size_t BufferSize>
struct has_get_stats<comm::link::MemoryLink<BufferSize>> : std::true_type {};

template<std::size_t QueueSize>
struct has_get_stats<comm::link::LoopbackLink<QueueSize>> : std::true_type {};

} // namespace comm::traits

#endif // COMM_LINK_RAW_HPP