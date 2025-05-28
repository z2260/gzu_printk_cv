#ifndef COMM_RINGBUF_H
#define COMM_RINGBUF_H

#include "config.h"
#include "frame.h"
#include "thread_config.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

    // 注意：COMM_RINGBUF_CRITICAL_ENTER/EXIT 现在在 thread_config.h 中定义
    // 如需禁用线程安全，请在编译时定义 COMM_CFG_THREAD_SAFE=0

    typedef struct {
        uint8_t* buffer;
        size_t   size;
        volatile size_t head;
        volatile size_t tail;
    } comm_ringbuf_t;

    void comm_ringbuf_init(comm_ringbuf_t* rb, uint8_t* buffer, size_t size);

    bool comm_ringbuf_put(comm_ringbuf_t* rb, uint8_t data);

    bool comm_ringbuf_get(comm_ringbuf_t* rb, uint8_t* data);

    size_t comm_ringbuf_write(comm_ringbuf_t* rb, const uint8_t* data, size_t len);

    size_t comm_ringbuf_read(comm_ringbuf_t* rb, uint8_t* data, size_t len);

    size_t comm_ringbuf_peek(const comm_ringbuf_t* rb, uint8_t* data, size_t len);

    void comm_ringbuf_clear(comm_ringbuf_t* rb);

    static inline bool comm_ringbuf_is_empty(const comm_ringbuf_t* rb) {
        return rb ? (rb->head == rb->tail) : true;
    }

    static inline bool comm_ringbuf_is_full(const comm_ringbuf_t* rb) {
        return rb ? (((rb->head + 1) % rb->size) == rb->tail) : true;
    }

    static inline size_t comm_ringbuf_available(const comm_ringbuf_t* rb) {
        if (!rb) return 0;
        return (rb->head >= rb->tail) ?
               (rb->head - rb->tail) :
               (rb->size - rb->tail + rb->head);
    }

    static inline size_t comm_ringbuf_free_space(const comm_ringbuf_t* rb) {
        return rb ? (rb->size - 1 - comm_ringbuf_available(rb)) : 0;
    }


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // COMM_RINGBUF_H