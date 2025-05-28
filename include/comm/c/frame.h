#ifndef COMM_FRAME_H
#define COMM_FRAME_H

#include "config.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// In comm_frame.h
#if defined(__GNUC__) || defined(__clang__)
#  define COMM_PACKED __attribute__((packed))
#elif defined(_MSC_VER) //
#  pragma pack(push,1)
#  define COMM_PACKED
#else //
#  define COMM_PACKED
#endif //

typedef struct COMM_PACKED {
    uint16_t magic;      /* 0-1: Magic 0xA55A */
    uint8_t version;     /* 2: Version */
    uint8_t flags;       /* 3: Flags */
    uint32_t length;     /* 4-7: Total frame length (with header) */
    uint32_t src_endpoint; /* 8-11: Source endpoint ID */
    uint32_t dst_endpoint; /* 12-15: Destination endpoint ID */
    uint32_t sequence;   /* 16-19: Sequence number/Related ID */
    uint32_t cmd_type;   /* 20-23: Command/Message type */
    uint32_t header_crc; /* 24-27: Header CRC */
    uint32_t payload_crc; /* 28-31: Payload CRC/Reserved */
} comm_frame_header_t;

#if defined(_MSC_VER)
#  pragma pack(pop)
#endif //

typedef struct {
    uint8_t  type;
    uint8_t  length;
    uint8_t  value[];
} comm_tlv_t;

typedef struct {
    const uint8_t* frame_data;  // Just store pointer, not actual data
    size_t frame_len;
    uint32_t sequence;
} comm_frame_ptr_t;

int comm_frame_encode(uint8_t* dst, size_t dst_size,
                      const uint8_t* payload, size_t payload_len,
                      const comm_frame_header_t* header);

int comm_frame_decode(const uint8_t* src, size_t src_len,
                      uint8_t* payload, size_t* payload_len,
                      comm_frame_header_t* header);
int comm_frame_validate(const comm_frame_header_t* header, size_t received_len);

uint16_t comm_crc16(const uint8_t* data, size_t len);
uint32_t comm_crc32(const uint8_t* data, size_t len);

int comm_tlv_add(uint8_t* buffer, size_t* offset, size_t max_size,
                 uint8_t type, const uint8_t* value, uint8_t value_len);

const comm_tlv_t* comm_tlv_find(const uint8_t* buffer, size_t len, uint8_t type);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // COMM_FRAME_H