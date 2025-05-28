#ifndef COMM_THREAD_CONFIG_H
#define COMM_THREAD_CONFIG_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 线程安全配置选项
#ifndef COMM_CFG_THREAD_SAFE
#define COMM_CFG_THREAD_SAFE 1  // 默认启用线程安全
#endif

#if COMM_CFG_THREAD_SAFE

// 平台特定的线程安全实现
#if defined(_WIN32) || defined(_WIN64)
    // Windows平台
    #include <windows.h>

    typedef CRITICAL_SECTION comm_mutex_t;

    #define COMM_MUTEX_INIT(mutex) InitializeCriticalSection(mutex)
    #define COMM_MUTEX_DESTROY(mutex) DeleteCriticalSection(mutex)
    #define COMM_MUTEX_LOCK(mutex) EnterCriticalSection(mutex)
    #define COMM_MUTEX_UNLOCK(mutex) LeaveCriticalSection(mutex)

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    // POSIX平台
    #include <pthread.h>

    typedef pthread_mutex_t comm_mutex_t;

    #define COMM_MUTEX_INIT(mutex) pthread_mutex_init(mutex, NULL)
    #define COMM_MUTEX_DESTROY(mutex) pthread_mutex_destroy(mutex)
    #define COMM_MUTEX_LOCK(mutex) pthread_mutex_lock(mutex)
    #define COMM_MUTEX_UNLOCK(mutex) pthread_mutex_unlock(mutex)

#elif defined(__STDC_NO_THREADS__)
    // 裸机或无线程支持的环境
    typedef int comm_mutex_t;  // 占位符

    #define COMM_MUTEX_INIT(mutex) (*(mutex) = 0)
    #define COMM_MUTEX_DESTROY(mutex) (*(mutex) = 0)
    #define COMM_MUTEX_LOCK(mutex) do { /* 禁用中断或其他平台特定操作 */ } while(0)
    #define COMM_MUTEX_UNLOCK(mutex) do { /* 启用中断或其他平台特定操作 */ } while(0)

#else
    // 其他平台 - 用户需要自定义实现
    #error "Unsupported platform for thread safety. Please define custom mutex operations."
#endif

// 全局ringbuf互斥锁（如果需要）
extern comm_mutex_t g_ringbuf_mutex;

// 初始化和清理函数
void comm_thread_init(void);
void comm_thread_cleanup(void);

// 重新定义ringbuf的临界区宏
#undef COMM_RINGBUF_CRITICAL_ENTER
#undef COMM_RINGBUF_CRITICAL_EXIT

#define COMM_RINGBUF_CRITICAL_ENTER() COMM_MUTEX_LOCK(&g_ringbuf_mutex)
#define COMM_RINGBUF_CRITICAL_EXIT() COMM_MUTEX_UNLOCK(&g_ringbuf_mutex)

#else
    // 线程安全被禁用
    typedef int comm_mutex_t;

    #define COMM_MUTEX_INIT(mutex) (*(mutex) = 0)
    #define COMM_MUTEX_DESTROY(mutex) (*(mutex) = 0)
    #define COMM_MUTEX_LOCK(mutex) do {} while(0)
    #define COMM_MUTEX_UNLOCK(mutex) do {} while(0)

    #define COMM_RINGBUF_CRITICAL_ENTER() do {} while(0)
    #define COMM_RINGBUF_CRITICAL_EXIT() do {} while(0)

    static inline void comm_thread_init(void) {}
    static inline void comm_thread_cleanup(void) {}

#endif // COMM_CFG_THREAD_SAFE

#ifdef __cplusplus
}
#endif

#endif // COMM_THREAD_CONFIG_H