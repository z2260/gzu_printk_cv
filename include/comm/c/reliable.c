#include "reliable.h"
#include "config.h"
#include <string.h>

void comm_reliable_init(comm_reliable_ctx_t* ctx, uint8_t window_size) {
    if (!ctx) {
        return;
    }

    memset(ctx, 0, sizeof(comm_reliable_ctx_t));

    if (window_size > COMM_CFG_MAX_WINDOW_SIZE) {
        window_size = COMM_CFG_MAX_WINDOW_SIZE;
    }

    if (window_size > 32) {
        window_size = 32;
    }

    ctx->window_size = window_size;
    ctx->rto = 1000;  // 默认重传超时1秒
}



int comm_reliable_on_send(comm_reliable_ctx_t* ctx,
                         const uint8_t* frame_data, size_t frame_len,
                         comm_frame_header_t* header,
                         uint32_t timestamp) {
    if (!ctx || !header) {
        return COMM_ERR_INVALID;
    }

    if (!comm_reliable_can_send(ctx)) {
        return COMM_ERR_OVERFLOW;
    }

    header->sequence = ctx->next_tx_seq;

    uint32_t window_index = ctx->next_tx_seq % ctx->window_size;
    if (frame_len <= COMM_CFG_MAX_FRAME_SIZE) {
        memcpy(ctx->tx_frames[window_index].frame_data, frame_data, frame_len);
        ctx->tx_frames[window_index].frame_len = frame_len;
        ctx->tx_frames[window_index].sequence = ctx->next_tx_seq;
    }

    // Update timestamp for RTO calculations
    ctx->tx_timestamp[window_index] = timestamp;

    uint32_t seq_offset = ctx->next_tx_seq - ctx->tx_window_base;
    if (seq_offset < 32) {
        ctx->tx_pending_mask |= (1U << seq_offset);
    }

    ctx->next_tx_seq++;
    return COMM_OK;
}

int comm_ack_build(const comm_frame_header_t* src_hdr,
                   uint32_t ack_seq,
                   comm_frame_header_t* ack_hdr)
{
    if (!src_hdr || !ack_hdr)
        return COMM_ERR_INVALID;

    memset(ack_hdr, 0, sizeof(*ack_hdr));
    ack_hdr->magic        = COMM_FRAME_MAGIC;
    ack_hdr->version      = COMM_FRAME_VERSION;
    ack_hdr->flags        = COMM_FLAG_ACK;
    ack_hdr->length       = COMM_FRAME_HEADER_SIZE;
    ack_hdr->sequence     = ack_seq;
    ack_hdr->src_endpoint = src_hdr->dst_endpoint;
    ack_hdr->dst_endpoint = src_hdr->src_endpoint;
    ack_hdr->cmd_type     = 0;

#if COMM_CFG_ENABLE_CRC32
    comm_frame_header_t le = *ack_hdr;
    le.magic        = COMM_HTOLE16(le.magic);
    le.length       = COMM_HTOLE32(le.length);
    le.src_endpoint = COMM_HTOLE32(le.src_endpoint);
    le.dst_endpoint = COMM_HTOLE32(le.dst_endpoint);
    le.sequence     = COMM_HTOLE32(le.sequence);
    le.cmd_type     = COMM_HTOLE32(le.cmd_type);
    le.header_crc   = 0;

    le.header_crc = COMM_HTOLE32(
        comm_crc32((const uint8_t*)&le,
                   COMM_FRAME_HEADER_SIZE - sizeof(le.header_crc)));

    *ack_hdr = le;
#else
    ack_hdr->header_crc = 0;
#endif
    return COMM_OK;
}

int comm_reliable_on_receive(comm_reliable_ctx_t* ctx,
                             const comm_frame_header_t* header,
                             comm_frame_header_t* ack_header) {
    if (!ctx || !header || !ack_header) {
        return COMM_ERR_INVALID;
    }

    uint32_t recv_seq = header->sequence;

    if (recv_seq == ctx->next_rx_seq) {
        ctx->next_rx_seq++;

        while (true) {
            uint32_t next_offset = ctx->next_rx_seq - ctx->rx_window_base;
            if (next_offset < 32 && (ctx->rx_received_mask & (1U << next_offset))) {
                ctx->rx_received_mask &= ~(1U << next_offset);
                ctx->next_rx_seq++;
            } else {
                break;
            }
        }

        while (ctx->next_rx_seq - ctx->rx_window_base >= ctx->window_size) {
            ctx->rx_window_base++;
            ctx->rx_received_mask >>= 1;
        }

    } else if (recv_seq > ctx->next_rx_seq) {
        uint32_t seq_offset = recv_seq - ctx->rx_window_base;
        if (seq_offset < ctx->window_size && seq_offset < 32) {
            // Check if this is the first time we see this out-of-order packet
            if (!(ctx->rx_received_mask & (1U << seq_offset))) {
                ctx->stat_out_of_order++;
                ctx->rx_received_mask |= (1U << seq_offset);
            } else {
                // Duplicate out-of-order packet
                ctx->stat_duplicates++;
            }
        } else {
            return COMM_ERR_INVALID;
        }
    } else {
        // recv_seq < ctx->next_rx_seq - duplicate packet
        ctx->stat_duplicates++;
    }

    return comm_ack_build(header, ctx->next_rx_seq - 1, ack_header);
}

int comm_reliable_on_ack(comm_reliable_ctx_t* ctx, const comm_frame_header_t* ack_header) {
    if (!ctx || !ack_header) {
        return COMM_ERR_INVALID;
    }

    if (!(ack_header->flags & COMM_FLAG_ACK)) {
        return COMM_ERR_INVALID;
    }

    uint32_t ack_seq = ack_header->sequence;

    if (ack_seq < ctx->tx_window_base) {
        return COMM_OK;
    }

    uint32_t shift = ack_seq - ctx->tx_window_base + 1;

    if (shift > 32) shift = 32;

    ctx->tx_pending_mask >>= shift;
    ctx->tx_window_base += shift;

    return COMM_OK;
}

void comm_reliable_poll(comm_reliable_ctx_t* ctx,
                       uint32_t current_time,
                       comm_retransmit_cb_t retransmit_cb,
                       void* user_data) {
    if (!ctx || !retransmit_cb) {
        return;
    }

    for (uint32_t i = 0; i < ctx->window_size && i < 32; i++) {
        if (ctx->tx_pending_mask & (1U << i)) {
            uint32_t seq = ctx->tx_window_base + i;
            uint32_t window_index = seq % ctx->window_size;

            if (current_time - ctx->tx_timestamp[window_index] > ctx->rto) {
                ctx->tx_timestamp[window_index] = current_time;
                ctx->stat_retransmits++;

                const comm_frame_cache_t* cache = &ctx->tx_frames[window_index];
                retransmit_cb(cache->frame_data, cache->frame_len, user_data);
            }
        }
    }
}

bool comm_reliable_can_send(const comm_reliable_ctx_t* ctx) {
    if (!ctx) {
        return false;
    }
    return (ctx->next_tx_seq - ctx->tx_window_base) < ctx->window_size;
}

void comm_reliable_get_stats(const comm_reliable_ctx_t* ctx,
                             comm_reliable_stats_t* stats) {
    if (!ctx || !stats) {
        return;
    }

    stats->tx_frames = ctx->next_tx_seq;
    stats->rx_frames = ctx->next_rx_seq;
    stats->retransmits = ctx->stat_retransmits;
    stats->duplicates = ctx->stat_duplicates;
    stats->out_of_order = ctx->stat_out_of_order;
}

void comm_reliable_reset_stats(comm_reliable_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    ctx->next_tx_seq = 0;
    ctx->next_rx_seq = 0;
    ctx->tx_window_base = 0;
    ctx->rx_window_base = 0;
    ctx->tx_pending_mask = 0;
    ctx->rx_received_mask = 0;
    ctx->stat_retransmits = 0;
    ctx->stat_duplicates = 0;
    ctx->stat_out_of_order = 0;

    memset(ctx->tx_timestamp, 0, sizeof(ctx->tx_timestamp));
}