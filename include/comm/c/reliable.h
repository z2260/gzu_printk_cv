#ifndef COMM_RELIABLE_H
#define COMM_RELIABLE_H

#include "config.h"
#include "frame.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
    uint8_t frame_data[COMM_CFG_MAX_FRAME_SIZE];
    size_t frame_len;
    uint32_t sequence;
} comm_frame_cache_t;

typedef struct {
    uint32_t next_tx_seq;      /* Next send sequence number */
    uint32_t next_rx_seq;      /* Expected receive sequence number */
    uint32_t tx_window_base;   /* Send window base address */
    uint32_t rx_window_base;   /* Receive window base address */
    uint8_t window_size;       /* Window size */
    uint32_t last_ack_time;    /* Last ACK time */
    uint32_t rto;              /* Retransmission timeout (ms) */

    uint32_t tx_pending_mask;  /* Pending frame mask */
    uint32_t tx_timestamp[COMM_CFG_MAX_WINDOW_SIZE]; /* Send timestamps */

    /* Frame cache for retransmission */
    comm_frame_cache_t tx_frames[COMM_CFG_MAX_WINDOW_SIZE];

    uint32_t rx_received_mask; /* Received frame mask */

    /* Statistics counters */
    uint32_t stat_retransmits;
    uint32_t stat_duplicates;
    uint32_t stat_out_of_order;
} comm_reliable_ctx_t;

typedef int (*comm_retransmit_cb_t)(const uint8_t* frame, size_t len, void* user_data);

void comm_reliable_init(comm_reliable_ctx_t* ctx, uint8_t window_size);

int comm_reliable_on_send(comm_reliable_ctx_t* ctx,
                         const uint8_t* frame_data, size_t frame_len,
                         comm_frame_header_t* header,
                         uint32_t timestamp);

int comm_reliable_on_receive(comm_reliable_ctx_t* ctx,
                            const comm_frame_header_t* header,
                            comm_frame_header_t* ack_header);

int comm_reliable_on_ack(comm_reliable_ctx_t* ctx,
                         const comm_frame_header_t* ack_header);

void comm_reliable_poll(comm_reliable_ctx_t* ctx,
                       uint32_t current_time,
                       comm_retransmit_cb_t retransmit_cb,
                       void* user_data);

bool comm_reliable_can_send(const comm_reliable_ctx_t* ctx);

int comm_ack_build(const comm_frame_header_t* src_hdr,
               uint32_t ack_seq,
               comm_frame_header_t* ack_hdr);

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t retransmits;
    uint32_t duplicates;
    uint32_t out_of_order;
} comm_reliable_stats_t;

void comm_reliable_get_stats(const comm_reliable_ctx_t* ctx,
                             comm_reliable_stats_t* stats);

void comm_reliable_reset_stats(comm_reliable_ctx_t* ctx);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // COMM_RELIABLE_H