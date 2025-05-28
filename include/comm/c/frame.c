#include "frame.h"
#include <string.h>

#if COMM_CFG_ENABLE_CRC16
static const uint16_t comm_crc16_table[256] = {
    0x0000u, 0xC0C1u, 0xC181u, 0x0140u, 0xC301u, 0x03C0u, 0x0280u, 0xC241u,
    0xC601u, 0x06C0u, 0x0780u, 0xC741u, 0x0500u, 0xC5C1u, 0xC481u, 0x0440u,
    0xCC01u, 0x0CC0u, 0x0D80u, 0xCD41u, 0x0F00u, 0xCFC1u, 0xCE81u, 0x0E40u,
    0x0A00u, 0xCAC1u, 0xCB81u, 0x0B40u, 0xC901u, 0x09C0u, 0x0880u, 0xC841u,
    0xD801u, 0x18C0u, 0x1980u, 0xD941u, 0x1B00u, 0xDBC1u, 0xDA81u, 0x1A40u,
    0x1E00u, 0xDEC1u, 0xDF81u, 0x1F40u, 0xDD01u, 0x1DC0u, 0x1C80u, 0xDC41u,
    0x1400u, 0xD4C1u, 0xD581u, 0x1540u, 0xD701u, 0x17C0u, 0x1680u, 0xD641u,
    0xD201u, 0x12C0u, 0x1380u, 0xD341u, 0x1100u, 0xD1C1u, 0xD081u, 0x1040u,
    0xF001u, 0x30C0u, 0x3180u, 0xF141u, 0x3300u, 0xF3C1u, 0xF281u, 0x3240u,
    0x3600u, 0xF6C1u, 0xF781u, 0x3740u, 0xF501u, 0x35C0u, 0x3480u, 0xF441u,
    0x3C00u, 0xFCC1u, 0xFD81u, 0x3D40u, 0xFF01u, 0x3FC0u, 0x3E80u, 0xFE41u,
    0xFA01u, 0x3AC0u, 0x3B80u, 0xFB41u, 0x3900u, 0xF9C1u, 0xF881u, 0x3840u,
    0x2800u, 0xE8C1u, 0xE981u, 0x2940u, 0xEB01u, 0x2BC0u, 0x2A80u, 0xEA41u,
    0xEE01u, 0x2EC0u, 0x2F80u, 0xEF41u, 0x2D00u, 0xEDC1u, 0xEC81u, 0x2C40u,
    0xE401u, 0x24C0u, 0x2580u, 0xE541u, 0x2700u, 0xE7C1u, 0xE681u, 0x2640u,
    0x2200u, 0xE2C1u, 0xE381u, 0x2340u, 0xE101u, 0x21C0u, 0x2080u, 0xE041u,
    0xA001u, 0x60C0u, 0x6180u, 0xA141u, 0x6300u, 0xA3C1u, 0xA281u, 0x6240u,
    0x6600u, 0xA6C1u, 0xA781u, 0x6740u, 0xA501u, 0x65C0u, 0x6480u, 0xA441u,
    0x6C00u, 0xACC1u, 0xAD81u, 0x6D40u, 0xAF01u, 0x6FC0u, 0x6E80u, 0xAE41u,
    0xAA01u, 0x6AC0u, 0x6B80u, 0xAB41u, 0x6900u, 0xA9C1u, 0xA881u, 0x6840u,
    0x7800u, 0xB8C1u, 0xB981u, 0x7940u, 0xBB01u, 0x7BC0u, 0x7A80u, 0xBA41u,
    0xBE01u, 0x7EC0u, 0x7F80u, 0xBF41u, 0x7D00u, 0xBDC1u, 0xBC81u, 0x7C40u,
    0xB401u, 0x74C0u, 0x7580u, 0xB541u, 0x7700u, 0xB7C1u, 0xB681u, 0x7640u,
    0x7200u, 0xB2C1u, 0xB381u, 0x7340u, 0xB101u, 0x71C0u, 0x7080u, 0xB041u,
    0x5000u, 0x90C1u, 0x9181u, 0x5140u, 0x9301u, 0x53C0u, 0x5280u, 0x9241u,
    0x9601u, 0x56C0u, 0x5780u, 0x9741u, 0x5500u, 0x95C1u, 0x9481u, 0x5440u,
    0x9C01u, 0x5CC0u, 0x5D80u, 0x9D41u, 0x5F00u, 0x9FC1u, 0x9E81u, 0x5E40u,
    0x5A00u, 0x9AC1u, 0x9B81u, 0x5B40u, 0x9901u, 0x59C0u, 0x5880u, 0x9841u,
    0x8801u, 0x48C0u, 0x4980u, 0x8941u, 0x4B00u, 0x8BC1u, 0x8A81u, 0x4A40u,
    0x4E00u, 0x8EC1u, 0x8F81u, 0x4F40u, 0x8D01u, 0x4DC0u, 0x4C80u, 0x8C41u,
    0x4400u, 0x84C1u, 0x8581u, 0x4540u, 0x8701u, 0x47C0u, 0x4680u, 0x8641u,
    0x8201u, 0x42C0u, 0x4380u, 0x8341u, 0x4100u, 0x81C1u, 0x8081u, 0x4040u
};
#endif // COMM_CFG_ENABLE_CRC16

#ifdef COMM_CFG_ENABLE_CRC32
static const uint32_t comm_crc32_table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
    0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u,
    0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
    0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
    0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u, 0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
    0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu, 0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
    0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
    0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
    0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
    0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au, 0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
    0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u, 0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
    0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u,
    0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u, 0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
    0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u, 0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
    0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
    0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
    0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
    0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u, 0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du
};
#endif // COMM_CFG_ENABLE_CRC32

uint16_t comm_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ comm_crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }

    return crc;
}

uint32_t comm_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        crc = comm_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return ~crc;
}

int comm_frame_validate(const comm_frame_header_t* header, size_t received_len) {
    if (!header) {
        return COMM_ERR_INVALID;
    }

    if (COMM_LETOH16(header->magic) != COMM_FRAME_MAGIC) {
        return COMM_ERR_INVALID;
    }

    if (header->version != COMM_FRAME_VERSION) {
        return COMM_ERR_INVALID;
    }

    uint32_t length = COMM_LETOH32(header->length);
    if (length < COMM_FRAME_HEADER_SIZE) {
        return COMM_ERR_INVALID;
    }

    if (length > COMM_CFG_MAX_FRAME_SIZE) {
        return COMM_ERR_INVALID;
    }

    if (received_len != length) {
        return COMM_ERR_INVALID;
    }

    return COMM_OK;
}

int comm_frame_encode(uint8_t* dst, size_t dst_size,
                      const uint8_t* payload, size_t payload_len,
                      const comm_frame_header_t* header) {
    if (!dst || !header) {
        return COMM_ERR_INVALID;
    }

    size_t total_len = COMM_FRAME_HEADER_SIZE + payload_len;
    if (dst_size < total_len) {
        return COMM_ERR_NOMEM;
    }

    // Create a copy of the header for modification
    comm_frame_header_t hdr = *header;
    hdr.length = (uint32_t)total_len;

    // Calculate payload CRC on raw bytes (endian-independent)
    if (payload && payload_len > 0) {
#if COMM_CFG_ENABLE_CRC32
        hdr.payload_crc = comm_crc32(payload, payload_len);
#else
        hdr.payload_crc = 0;
#endif
    } else {
        hdr.payload_crc = 0;
    }

#if COMM_CFG_ENABLE_CRC32
    // Convert header fields to little endian for CRC calculation
    comm_frame_header_t le_hdr = hdr;
    le_hdr.magic = COMM_HTOLE16(le_hdr.magic);
    le_hdr.length = COMM_HTOLE32(le_hdr.length);
    le_hdr.src_endpoint = COMM_HTOLE32(le_hdr.src_endpoint);
    le_hdr.dst_endpoint = COMM_HTOLE32(le_hdr.dst_endpoint);
    le_hdr.sequence = COMM_HTOLE32(le_hdr.sequence);
    le_hdr.cmd_type = COMM_HTOLE32(le_hdr.cmd_type);
    le_hdr.payload_crc = COMM_HTOLE32(le_hdr.payload_crc);
    le_hdr.header_crc = 0;

    // Calculate header CRC on little endian data
    le_hdr.header_crc = COMM_HTOLE32(
        comm_crc32((const uint8_t*)&le_hdr, COMM_FRAME_HEADER_SIZE - sizeof(le_hdr.header_crc)));

    // Copy the little endian header to destination
    memcpy(dst, &le_hdr, COMM_FRAME_HEADER_SIZE);
#else
    // Convert header fields to little endian even without CRC
    comm_frame_header_t le_hdr = hdr;
    le_hdr.magic = COMM_HTOLE16(le_hdr.magic);
    le_hdr.length = COMM_HTOLE32(le_hdr.length);
    le_hdr.src_endpoint = COMM_HTOLE32(le_hdr.src_endpoint);
    le_hdr.dst_endpoint = COMM_HTOLE32(le_hdr.dst_endpoint);
    le_hdr.sequence = COMM_HTOLE32(le_hdr.sequence);
    le_hdr.cmd_type = COMM_HTOLE32(le_hdr.cmd_type);
    le_hdr.payload_crc = COMM_HTOLE32(le_hdr.payload_crc);
    le_hdr.header_crc = 0;

    memcpy(dst, &le_hdr, COMM_FRAME_HEADER_SIZE);
#endif

    // Copy payload (endian-independent)
    if (payload && payload_len > 0) {
        memcpy(dst + COMM_FRAME_HEADER_SIZE, payload, payload_len);
    }

    return (int)total_len;
}

int comm_frame_decode(const uint8_t* src, size_t src_len,
                      uint8_t* payload, size_t* payload_len,
                      comm_frame_header_t* header)
{
    if (!src || !header || !payload_len) {
        return COMM_ERR_INVALID;
    }

    if (src_len < COMM_FRAME_HEADER_SIZE) {
        return COMM_ERR_INVALID;
    }

    // Copy little endian data from network buffer
    comm_frame_header_t le_header;
    memcpy(&le_header, src, COMM_FRAME_HEADER_SIZE);

    // Convert from little endian to native endian for validation and use
    header->magic = COMM_LETOH16(le_header.magic);
    header->version = le_header.version;  // single byte, no conversion needed
    header->flags = le_header.flags;      // single byte, no conversion needed
    header->length = COMM_LETOH32(le_header.length);
    header->src_endpoint = COMM_LETOH32(le_header.src_endpoint);
    header->dst_endpoint = COMM_LETOH32(le_header.dst_endpoint);
    header->sequence = COMM_LETOH32(le_header.sequence);
    header->cmd_type = COMM_LETOH32(le_header.cmd_type);
    header->payload_crc = COMM_LETOH32(le_header.payload_crc);
    header->header_crc = COMM_LETOH32(le_header.header_crc);

    // Validate using native endian values
    int result = comm_frame_validate(header, src_len);
    if (result != COMM_OK) {
        return result;
    }

    uint32_t frame_len = header->length;

    if (src_len < frame_len) {
        return COMM_ERR_INVALID;
    }

#if COMM_CFG_ENABLE_CRC32
    // Verify header CRC using the original little endian data
    uint32_t saved_header_crc = le_header.header_crc;
    le_header.header_crc = 0;
    uint32_t calc_header_crc = comm_crc32((const uint8_t*)&le_header,
                                         COMM_FRAME_HEADER_SIZE - sizeof(le_header.header_crc));

    if (calc_header_crc != COMM_LETOH32(saved_header_crc)) {
        return COMM_ERR_CRC;
    }
#endif

    size_t actual_payload_len = frame_len - COMM_FRAME_HEADER_SIZE;

    if (actual_payload_len > 0) {
        if (!payload || *payload_len < actual_payload_len) {
            return COMM_ERR_NOMEM;
        }

        memcpy(payload, src + COMM_FRAME_HEADER_SIZE, actual_payload_len);

#if COMM_CFG_ENABLE_CRC32
        // Verify payload CRC (payload is endian-independent raw bytes)
        uint32_t calc_payload_crc = comm_crc32(payload, actual_payload_len);
        if (calc_payload_crc != header->payload_crc) {
            return COMM_ERR_CRC;
        }
#endif
    }

    *payload_len = actual_payload_len;
    return COMM_OK;
}

int comm_tlv_add(uint8_t* buffer, size_t* offset, size_t max_size,
                 uint8_t type, const uint8_t* value, uint8_t value_len) {
    if (!buffer || !offset) {
        return COMM_ERR_INVALID;
    }

    size_t required_size = *offset + 2 + value_len; // type + length + value
    if (required_size > max_size) {
        return COMM_ERR_NOMEM;
    }

    buffer[*offset] = type;
    buffer[*offset + 1] = value_len;

    if (value && value_len > 0) {
        memcpy(buffer + *offset + 2, value, value_len);
    }

    *offset = required_size;
    return COMM_OK;
}

const comm_tlv_t* comm_tlv_find(const uint8_t* buffer, size_t len, uint8_t type) {
    if (!buffer) {
        return NULL;
    }

    size_t offset = 0;
    while (offset + 2 <= len) {
        const comm_tlv_t* tlv = (const comm_tlv_t*)(buffer + offset);

        if (tlv->type == type) {
            if (offset + 2 + tlv->length <= len) {
                return tlv;
            } else {
                return NULL;
            }
        }

        offset += 2 + tlv->length;
    }

    return NULL;
}

#if defined(__x86_64__) && defined(__SSE4_2__)
#include <nmmintrin.h>

uint32_t comm_crc32_sse42(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;

    // Process 8-byte chunks using SSE4.2 CRC32 instruction
    while (len >= 8) {
        uint64_t chunk;
        memcpy(&chunk, data, 8);
        crc = (uint32_t)_mm_crc32_u64(crc, chunk);
        data += 8;
        len -= 8;
    }

    // Process 4-byte chunks
    while (len >= 4) {
        uint32_t chunk;
        memcpy(&chunk, data, 4);
        crc = _mm_crc32_u32(crc, chunk);
        data += 4;
        len -= 4;
    }

    // Process remaining bytes
    while (len > 0) {
        crc = _mm_crc32_u8(crc, *data);
        data++;
        len--;
    }

    return ~crc;
}
#endif // defined(__x86_64__) && defined(__SSE4_2__)