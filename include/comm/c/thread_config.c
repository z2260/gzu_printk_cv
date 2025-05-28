#include "thread_config.h"

#if COMM_CFG_THREAD_SAFE

// 全局ringbuf互斥锁
comm_mutex_t g_ringbuf_mutex;

void comm_thread_init(void) {
    COMM_MUTEX_INIT(&g_ringbuf_mutex);
}

void comm_thread_cleanup(void) {
    COMM_MUTEX_DESTROY(&g_ringbuf_mutex);
}

#endif // COMM_CFG_THREAD_SAFE