#include "ringbuf.h"
#include <string.h>

void comm_ringbuf_init(comm_ringbuf_t* rb, uint8_t* buffer, size_t size) {
    if (!rb || !buffer || size == 0) {
        return;
    }

    rb->buffer = buffer;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

bool comm_ringbuf_put(comm_ringbuf_t* rb, uint8_t data) {
    if (!rb) {
        return false;
    }

    bool result;
    COMM_RINGBUF_CRITICAL_ENTER();

    size_t next_head = (rb->head + 1) % rb->size;
    if (next_head == rb->tail) {
        result = false; // Buffer full
    } else {
        rb->buffer[rb->head] = data;
        rb->head = next_head;
        result = true;
    }

    COMM_RINGBUF_CRITICAL_EXIT();
    return result;
}

bool comm_ringbuf_get(comm_ringbuf_t* rb, uint8_t* data) {
    if (!rb || !data) {
        return false;
    }

    if (rb->head == rb->tail) {
        return false; // 缓冲区空
    }

    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return true;
}

size_t comm_ringbuf_write(comm_ringbuf_t* rb, const uint8_t* data, size_t len) {
    if (!rb || !data) {
        return 0;
    }

    size_t free = comm_ringbuf_free_space(rb);
    size_t to_write = (len < free) ? len : free;

    COMM_RINGBUF_CRITICAL_ENTER();

    // Handle wrapping correctly
    size_t space_till_end = rb->size - rb->head;
    if (to_write <= space_till_end) {
        // Single chunk
        memcpy(rb->buffer + rb->head, data, to_write);
        rb->head = (rb->head + to_write) % rb->size;
    } else {
        // Two chunks needed
        memcpy(rb->buffer + rb->head, data, space_till_end);
        memcpy(rb->buffer, data + space_till_end, to_write - space_till_end);
        rb->head = to_write - space_till_end;
    }

    COMM_RINGBUF_CRITICAL_EXIT();
    return to_write;
}

size_t comm_ringbuf_read(comm_ringbuf_t* rb, uint8_t* data, size_t len) {
    if (!rb || !data) {
        return 0;
    }

    size_t available = comm_ringbuf_available(rb);
    size_t to_read = (len < available) ? len : available;

    if (to_read == 0) {
        return 0;
    }

    COMM_RINGBUF_CRITICAL_ENTER();

    // Handle wrapping correctly
    size_t space_till_end = rb->size - rb->tail;
    if (to_read <= space_till_end) {
        // Single chunk
        memcpy(data, rb->buffer + rb->tail, to_read);
        rb->tail = (rb->tail + to_read) % rb->size;
    } else {
        // Two chunks needed
        memcpy(data, rb->buffer + rb->tail, space_till_end);
        memcpy(data + space_till_end, rb->buffer, to_read - space_till_end);
        rb->tail = to_read - space_till_end;
    }

    COMM_RINGBUF_CRITICAL_EXIT();
    return to_read;
}

size_t comm_ringbuf_peek(const comm_ringbuf_t* rb, uint8_t* data, size_t len) {
    if (!rb || !data) {
        return 0;
    }

    COMM_RINGBUF_CRITICAL_ENTER();

    size_t available = comm_ringbuf_available(rb);
    size_t to_peek = (len < available) ? len : available;

    size_t tail = rb->tail;
    for (size_t i = 0; i < to_peek; i++) {
        data[i] = rb->buffer[tail];
        tail = (tail + 1) % rb->size;
    }

    COMM_RINGBUF_CRITICAL_EXIT();
    return to_peek;
}

void comm_ringbuf_clear(comm_ringbuf_t* rb) {
    if (!rb) {
        return;
    }

    rb->head = 0;
    rb->tail = 0;
}
