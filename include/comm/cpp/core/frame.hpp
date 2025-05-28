#pragma once

#ifndef COMM_CORE_FRAME_HPP
#define COMM_CORE_FRAME_HPP

#include "endpoint.hpp"
#include "../../c/frame.h"
#include <vector>
#include <optional>
#include <cstring>
#include <algorithm>

namespace comm::core {

class Frame {
public:
    Frame() {
        std::memset(&header_, 0, sizeof(header_));
        header_.magic = COMM_FRAME_MAGIC;
        header_.version = COMM_FRAME_VERSION;
    }

    explicit Frame(const comm_frame_header_t& c_header) : header_(c_header) {}

    std::uint16_t magic() const noexcept {
        return header_.magic;
    }

    std::uint8_t version() const noexcept {
        return header_.version;
    }

    std::uint8_t flags() const noexcept {
        return header_.flags;
    }

    std::uint32_t length() const noexcept {
        return header_.length;
    }

    std::uint32_t sequence() const noexcept {
        return header_.sequence;
    }

    MessageType message_type() const noexcept {
        return static_cast<MessageType>(header_.cmd_type);
    }

    EndpointID src_endpoint() const noexcept {
        return EndpointID{header_.src_endpoint, 0, 0, 0};
    }

    EndpointID dst_endpoint() const noexcept {
        return EndpointID{header_.dst_endpoint, 0, 0, 0};
    }

    void set_flags(std::uint8_t flags) noexcept {
        header_.flags = flags;
    }

    void set_length(std::uint32_t length) noexcept {
        header_.length = length;
    }

    void set_sequence(std::uint32_t seq) noexcept {
        header_.sequence = seq;
    }

    void set_message_type(MessageType type) noexcept {
        header_.cmd_type = static_cast<std::uint32_t>(type);
    }

    void set_src_endpoint(const EndpointID& ep) noexcept {
        header_.src_endpoint = ep.node_id;
    }

    void set_dst_endpoint(const EndpointID& ep) noexcept {
        header_.dst_endpoint = ep.node_id;
    }

    bool has_flag(std::uint8_t flag) const noexcept {
        return (header_.flags & flag) != 0;
    }

    void set_flag(std::uint8_t flag) noexcept {
        header_.flags |= flag;
    }

    void clear_flag(std::uint8_t flag) noexcept {
        header_.flags &= ~flag;
    }

    bool is_compressed() const noexcept {
        return has_flag(COMM_FLAG_COMPRESSED);
    }

    bool is_encrypted() const noexcept {
        return has_flag(COMM_FLAG_ENCRYPTED);
    }

    bool is_zero_copy() const noexcept {
        return has_flag(COMM_FLAG_ZERO_COPY);
    }

    bool is_fragmented() const noexcept {
        return has_flag(COMM_FLAG_FRAGMENTED);
    }

    bool is_ack() const noexcept {
        return has_flag(COMM_FLAG_ACK);
    }

    bool is_heartbeat() const noexcept {
        return has_flag(COMM_FLAG_HEARTBEAT);
    }

    void mark_compressed() noexcept {
        set_flag(COMM_FLAG_COMPRESSED);
    }

    void mark_encrypted() noexcept {
        set_flag(COMM_FLAG_ENCRYPTED);
    }

    void mark_zero_copy() noexcept {
        set_flag(COMM_FLAG_ZERO_COPY);
    }

    void mark_fragmented() noexcept {
        set_flag(COMM_FLAG_FRAGMENTED);
    }

    void mark_ack() noexcept {
        set_flag(COMM_FLAG_ACK);
    }

    void mark_heartbeat() noexcept {
        set_flag(COMM_FLAG_HEARTBEAT);
    }

    const comm_frame_header_t& c_header() const noexcept {
        return header_;
    }

    comm_frame_header_t& c_header() noexcept {
        return header_;
    }

    comm_frame_header_t* c_header_ptr() noexcept {
        return &header_;
    }

    bool is_valid() const noexcept {
        return comm_frame_validate(&header_, header_.length) == COMM_OK;
    }

private:
    comm_frame_header_t header_;
};

class FrameCodec {
public:
    static constexpr std::uint32_t MAX_FRAME_LENGTH = COMM_CFG_MAX_FRAME_SIZE; // Use C layer's limit for consistency

    static std::optional<std::vector<std::uint8_t>>
    encode(const Frame& frame, const std::uint8_t* payload, size_t payload_size) {
        std::vector<std::uint8_t> buffer(COMM_FRAME_HEADER_SIZE + payload_size);

        Frame mutable_frame = frame;
        int result = comm_frame_encode(
            buffer.data(), buffer.size(),
            payload, payload_size,
            mutable_frame.c_header_ptr()
        );

        if (result < 0) {
            return std::nullopt;
        }

        buffer.resize(result);
        return buffer;
    }

    static std::optional<std::pair<Frame, std::vector<std::uint8_t>>>
    decode(const std::uint8_t* buffer, size_t buffer_size) {
        Frame frame;
        std::vector<std::uint8_t> payload(buffer_size);
        size_t payload_len = payload.size();

        int result = comm_frame_decode(
            buffer, buffer_size,
            payload.data(), &payload_len,
            frame.c_header_ptr()
        );

        if (result < 0) {
            return std::nullopt;
        }

        payload.resize(payload_len);
        return std::make_pair(std::move(frame), std::move(payload));
    }

    static std::optional<std::pair<Frame, std::vector<std::uint8_t>>>
    try_decode_stream(const std::uint8_t* buffer, size_t buffer_size, size_t& consumed) {
        if (buffer_size < COMM_FRAME_HEADER_SIZE) {
            consumed = 0;
            return std::nullopt;
        }

        // Read frame length with proper endian conversion
        std::uint32_t frame_length_le;
        std::memcpy(&frame_length_le, buffer + 4, sizeof(frame_length_le));
        std::uint32_t frame_length = COMM_LETOH32(frame_length_le);

        if (frame_length > MAX_FRAME_LENGTH || frame_length < COMM_FRAME_HEADER_SIZE) {
            consumed = 0;
            return std::nullopt;
        }

        if (buffer_size < frame_length) {
            consumed = 0;
            return std::nullopt;
        }

        auto result = decode(buffer, frame_length);
        if (result) {
            consumed = frame_length;
        } else {
            consumed = 0;
        }

        return result;
    }
};

class TLVExtension {
public:
    struct Entry {
        std::uint8_t type;
        std::vector<std::uint8_t> value;
    };

    void add(std::uint8_t type, const std::uint8_t* value, size_t value_size) {
        if (value_size > MAX_VALUE_SIZE) {
            return;
        }
        entries_.emplace_back(Entry{type, std::vector<std::uint8_t>(value, value + value_size)});
    }

    std::optional<std::pair<const std::uint8_t*, size_t>> find(std::uint8_t type) const {
        for (const auto& entry : entries_) {
            if (entry.type == type) {
                return std::make_pair(entry.value.data(), entry.value.size());
            }
        }
        return std::nullopt;
    }

    std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> result;
        for (const auto& entry : entries_) {
            result.push_back(entry.type);

            if (entry.value.size() <= 255) {
                result.push_back(static_cast<std::uint8_t>(entry.value.size()));
            } else {
                result.push_back(0xFF);
                std::uint16_t length = static_cast<std::uint16_t>(
                    std::min(entry.value.size(), static_cast<size_t>(MAX_VALUE_SIZE)));
                result.push_back(static_cast<std::uint8_t>(length & 0xFF));
                result.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFF));
            }

            result.insert(result.end(), entry.value.begin(), entry.value.end());
        }
        return result;
    }

    static std::optional<TLVExtension> deserialize(const std::uint8_t* data, size_t data_size) {
        TLVExtension ext;
        size_t offset = 0;

        while (offset + 2 <= data_size) {
            std::uint8_t type = data[offset++];
            std::uint8_t length_byte = data[offset++];

            std::uint16_t length;
            if (length_byte == 0xFF) {
                if (offset + 2 > data_size) {
                    return std::nullopt;
                }
                length = static_cast<std::uint16_t>(data[offset]) |
                        (static_cast<std::uint16_t>(data[offset + 1]) << 8);
                offset += 2;
            } else {
                length = length_byte;
            }

            if (offset + length > data_size || length > MAX_VALUE_SIZE) {
                return std::nullopt;
            }

            ext.add(type, data + offset, length);
            offset += length;
        }

        return ext;
    }

    const std::vector<Entry>& entries() const noexcept {
        return entries_;
    }

    void clear() noexcept {
        entries_.clear();
    }

    bool empty() const noexcept {
        return entries_.empty();
    }

private:
    static constexpr std::uint16_t MAX_VALUE_SIZE = 32768; // 32KB最大值大小
    std::vector<Entry> entries_;
};

} // namespace comm::core

#endif // COMM_CORE_FRAME_HPP