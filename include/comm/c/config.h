#ifndef COMM_CONFIG_H
#define COMM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*                              平台配置                                      */
/* ========================================================================== */

// 是否使用平台适配层
#ifndef COMM_CFG_USE_PLATFORM_HAL
#define COMM_CFG_USE_PLATFORM_HAL 1
#endif

// 是否启用调试功能
#ifndef COMM_CFG_ENABLE_DEBUG
#define COMM_CFG_ENABLE_DEBUG 0
#endif

// 是否使用自定义内存管理
#ifndef COMM_CFG_USE_CUSTOM_MALLOC
#define COMM_CFG_USE_CUSTOM_MALLOC 0
#endif

// MCU资源受限模式（减少内存使用，优化性能）
#ifndef COMM_CFG_MCU_CONSTRAINED
#define COMM_CFG_MCU_CONSTRAINED 0
#endif

/* ========================================================================== */
/*                              CRC配置                                       */
/* ========================================================================== */

// 前向声明CRC函数
uint32_t comm_crc32(const uint8_t* data, size_t len);

#if defined(COMM_USE_HW_CRC)
  extern uint32_t comm_hw_crc32(const void* buf, size_t len);
  #define COMM_CALC_CRC32 comm_hw_crc32
#elif defined(__x86_64__) && defined(__SSE4_2__) //
  uint32_t comm_crc32_sse42(const uint8_t* data, size_t len);
  #define COMM_CALC_CRC32 comm_crc32_sse42
#else //
  #define COMM_CALC_CRC32 comm_crc32
#endif //

/* ========================================================================== */
/*                              缓冲区大小配置                                */
/* ========================================================================== */

#ifndef COMM_CFG_MAX_FRAME_SIZE
#if COMM_CFG_MCU_CONSTRAINED
#define COMM_CFG_MAX_FRAME_SIZE 256  // MCU受限模式：较小的帧大小
#else
#define COMM_CFG_MAX_FRAME_SIZE 1024 // 标准模式
#endif
#endif // COMM_CFG_MAX_FRAME_SIZE

#ifndef COMM_CFG_MAX_WINDOW_SIZE
#if COMM_CFG_MCU_CONSTRAINED
#define COMM_CFG_MAX_WINDOW_SIZE 4   // MCU受限模式：较小的窗口
#else
#define COMM_CFG_MAX_WINDOW_SIZE 16  // 标准模式
#endif
#endif // COMM_CFG_MAX_WINDOW_SIZE

#ifndef COMM_CFG_RINGBUF_SIZE
#if COMM_CFG_MCU_CONSTRAINED
#define COMM_CFG_RINGBUF_SIZE 512    // MCU受限模式：较小的环形缓冲区
#else
#define COMM_CFG_RINGBUF_SIZE 2048   // 标准模式
#endif
#endif // COMM_CFG_RINGBUF_SIZE

/* ========================================================================== */
/*                              功能开关                                      */
/* ========================================================================== */

#ifndef COMM_CFG_ENABLE_CRC16
#define COMM_CFG_ENABLE_CRC16 1
#endif // COMM_CFG_ENABLE_CRC16

#ifndef COMM_CFG_ENABLE_CRC32
#define COMM_CFG_ENABLE_CRC32 1
#endif // COMM_CFG_ENABLE_CRC32

// 是否启用帧压缩（需要额外的内存和CPU）
#ifndef COMM_CFG_ENABLE_COMPRESSION
#if COMM_CFG_MCU_CONSTRAINED
#define COMM_CFG_ENABLE_COMPRESSION 0  // MCU受限模式：禁用压缩
#else
#define COMM_CFG_ENABLE_COMPRESSION 1  // 标准模式：启用压缩
#endif
#endif

// 是否启用帧加密（需要额外的内存和CPU）
#ifndef COMM_CFG_ENABLE_ENCRYPTION
#if COMM_CFG_MCU_CONSTRAINED
#define COMM_CFG_ENABLE_ENCRYPTION 0   // MCU受限模式：禁用加密
#else
#define COMM_CFG_ENABLE_ENCRYPTION 1   // 标准模式：启用加密
#endif
#endif

/* ========================================================================== */
/*                              协议常量                                      */
/* ========================================================================== */

#define COMM_FRAME_MAGIC 0xA55A
#define COMM_FRAME_VERSION 1
#define COMM_FRAME_HEADER_SIZE 32

#define COMM_FLAG_COMPRESSED    (1 << 0)
#define COMM_FLAG_ENCRYPTED     (1 << 1)
#define COMM_FLAG_ZERO_COPY     (1 << 2)
#define COMM_FLAG_FRAGMENTED    (1 << 3)
#define COMM_FLAG_ACK           (1 << 4)
#define COMM_FLAG_NACK          (1 << 5)
#define COMM_FLAG_HEARTBEAT     (1 << 6)
#define COMM_FLAG_EXTENDED_HDR  (1 << 7)

/* ========================================================================== */
/*                              错误码定义                                    */
/* ========================================================================== */

#define COMM_OK             0
#define COMM_ERR_INVALID   -1
#define COMM_ERR_NOMEM     -2
#define COMM_ERR_TIMEOUT   -3
#define COMM_ERR_CRC       -4
#define COMM_ERR_OVERFLOW  -5
#define COMM_ERR_PLATFORM  -6  // 平台相关错误

/* ========================================================================== */
/*                              字节序处理                                    */
/* ========================================================================== */

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  ifdef HAVE_BYTESWAP_H
#    include <byteswap.h>
#    define COMM_HTOLE16(x) bswap_16((uint16_t)(x))
#    define COMM_HTOLE32(x) bswap_32((uint32_t)(x))
#  else //
#    define COMM_HTOLE16(x) ((uint16_t)((((uint16_t)(x) & 0xff00U) >> 8) | \
(((uint16_t)(x) & 0x00ffU) << 8)))
#    define COMM_HTOLE32(x) ((uint32_t)( \
(((uint32_t)(x) & 0x000000ffU) << 24) | \
(((uint32_t)(x) & 0x0000ff00U) <<  8) | \
(((uint32_t)(x) & 0x00ff0000U) >>  8) | \
(((uint32_t)(x) & 0xff000000U) >> 24)))
#  endif //
#  define COMM_LETOH16  COMM_HTOLE16
#  define COMM_LETOH32  COMM_HTOLE32
#else //
#  define COMM_HTOLE16(x) ((uint16_t)(x))
#  define COMM_HTOLE32(x) ((uint32_t)(x))
#  define COMM_LETOH16(x) ((uint16_t)(x))
#  define COMM_LETOH32(x) ((uint32_t)(x))
#endif //

/* ========================================================================== */
/*                              时间源配置                                    */
/* ========================================================================== */

#if COMM_CFG_USE_PLATFORM_HAL
// 使用平台适配层的时间函数
extern uint32_t comm_platform_get_ms_tick(void);
#define comm_get_time_ms() comm_platform_get_ms_tick()
#else
// 用户需要自己定义时间函数
extern uint32_t comm_get_time_ms(void);
#endif

/* ========================================================================== */
/*                              临界区配置                                    */
/* ========================================================================== */

#if COMM_CFG_USE_PLATFORM_HAL
// 使用平台适配层的临界区保护
extern void comm_platform_critical_enter(void);
extern void comm_platform_critical_exit(void);
#define COMM_RINGBUF_CRITICAL_ENTER() comm_platform_critical_enter()
#define COMM_RINGBUF_CRITICAL_EXIT()  comm_platform_critical_exit()
#else
// 默认的临界区保护（用户需要根据平台修改）
#define COMM_RINGBUF_CRITICAL_ENTER() do { /* 用户实现：禁用中断 */ } while(0)
#define COMM_RINGBUF_CRITICAL_EXIT()  do { /* 用户实现：恢复中断 */ } while(0)
#endif

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // COMM_CONFIG_H