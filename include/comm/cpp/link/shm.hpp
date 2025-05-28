#pragma once

#ifndef COMM_LINK_SHM_HPP
#define COMM_LINK_SHM_HPP

#include "../core/traits.hpp"
#include "../core/endpoint.hpp"
#include "raw.hpp"
#include <optional>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else // _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#endif // _WIN32

namespace comm::link {

struct ShmMutex {
#ifdef _WIN32
    HANDLE mutex_handle;
    char name[64];
#else
    pthread_mutex_t mutex;
    pthread_mutexattr_t attr;
#endif

    void init(const char* name_prefix, uint32_t id) {
#ifdef _WIN32
        snprintf(name, sizeof(name), "Global\\%s_mutex_%u", name_prefix, id);
        mutex_handle = CreateMutexA(nullptr, FALSE, name);
#else
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mutex, &attr);
#endif
    }

    void destroy() {
#ifdef _WIN32
        if (mutex_handle != nullptr) {
            CloseHandle(mutex_handle);
            mutex_handle = nullptr;
        }
#else
        pthread_mutex_destroy(&mutex);
        pthread_mutexattr_destroy(&attr);
#endif
    }

    bool lock(uint32_t timeout_ms = 1000) {
#ifdef _WIN32
        DWORD result = WaitForSingleObject(mutex_handle, timeout_ms);
        return result == WAIT_OBJECT_0;
#else
        if (timeout_ms == UINT32_MAX) {
            return pthread_mutex_lock(&mutex) == 0;
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            return pthread_mutex_timedlock(&mutex, &ts) == 0;
        }
#endif
    }

    void unlock() {
#ifdef _WIN32
        ReleaseMutex(mutex_handle);
#else
        pthread_mutex_unlock(&mutex);
#endif
    }
};

// RAII锁守卫
class ShmLockGuard {
    ShmMutex* mutex_;
    bool locked_;

public:
    explicit ShmLockGuard(ShmMutex* mutex, uint32_t timeout_ms = 1000)
        : mutex_(mutex), locked_(false) {
        if (mutex_) {
            locked_ = mutex_->lock(timeout_ms);
        }
    }

    ~ShmLockGuard() {
        if (locked_ && mutex_) {
            mutex_->unlock();
        }
    }

    bool is_locked() const { return locked_; }

    // 禁用拷贝
    ShmLockGuard(const ShmLockGuard&) = delete;
    ShmLockGuard& operator=(const ShmLockGuard&) = delete;
};

// 消息头结构
struct ShmMessageHeader {
    uint32_t length;        // 消息长度
    uint32_t sender_id;     // 发送者端点ID
    uint32_t sequence;      // 序列号
    uint32_t timestamp;     // 时间戳
    uint32_t crc32;         // CRC校验
    uint8_t flags;          // 标志位
    uint8_t reserved[3];    // 保留字段
};

// 多读者环形缓冲区（支持一对多）
struct ShmMultiReaderRingBuffer {
    // 添加缓存行对齐，避免false sharing
    alignas(64) std::atomic<std::uint32_t> write_pos;
    std::uint32_t capacity;
    std::uint32_t mask;
    std::uint32_t max_readers;

    // 每个读者的读取位置
    struct ReaderState {
        alignas(64) std::atomic<std::uint32_t> read_pos;  // 缓存行对齐
        std::atomic<bool> active;
        uint32_t reader_id;
        uint32_t last_access_time;
    };

    ReaderState readers[];
    // 数据区域在readers数组之后

    static bool is_power_of_two(std::uint32_t n) {
        return n > 0 && (n & (n - 1)) == 0;
    }

    void init(std::uint32_t cap, std::uint32_t max_readers_count) {
        if (!is_power_of_two(cap)) {
            throw std::invalid_argument("Capacity must be power of 2");
        }
        capacity = cap;
        mask = cap - 1;
        max_readers = max_readers_count;
        write_pos.store(0, std::memory_order_relaxed);

        for (std::uint32_t i = 0; i < max_readers; ++i) {
            readers[i].read_pos.store(0, std::memory_order_relaxed);
            readers[i].active.store(false, std::memory_order_relaxed);
            readers[i].reader_id = UINT32_MAX;
            readers[i].last_access_time = 0;
        }
    }

    uint8_t* get_data_ptr() {
        return reinterpret_cast<uint8_t*>(readers + max_readers);
    }

    const uint8_t* get_data_ptr() const {
        return reinterpret_cast<const uint8_t*>(readers + max_readers);
    }

    // 注册读者
    bool register_reader(uint32_t reader_id) {
        for (std::uint32_t i = 0; i < max_readers; ++i) {
            bool expected = false;
            // 使用strong版本避免spurious failure
            if (readers[i].active.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                readers[i].reader_id = reader_id;
                readers[i].read_pos.store(write_pos.load(std::memory_order_acquire), std::memory_order_release);
                readers[i].last_access_time = static_cast<uint32_t>(std::time(nullptr));
                return true;
            }
        }
        return false; // 没有可用的读者槽位
    }

    // 注销读者
    void unregister_reader(uint32_t reader_id) {
        for (std::uint32_t i = 0; i < max_readers; ++i) {
            if (readers[i].reader_id == reader_id && readers[i].active.load(std::memory_order_acquire)) {
                readers[i].active.store(false, std::memory_order_release);
                readers[i].reader_id = UINT32_MAX;
            }
        }
    }

    // 获取最慢读者的位置（用于确定可覆盖的数据）
    std::uint32_t get_slowest_reader_pos() const {
        std::uint32_t min_pos = write_pos.load(std::memory_order_acquire);

        for (std::uint32_t i = 0; i < max_readers; ++i) {
            if (readers[i].active.load(std::memory_order_acquire)) {
                std::uint32_t reader_pos = readers[i].read_pos.load(std::memory_order_acquire);
                if (reader_pos < min_pos) {
                    min_pos = reader_pos;
                }
            }
        }

        return min_pos;
    }

    // 获取可用写入空间
    std::uint32_t available_write() const {
        std::uint32_t w = write_pos.load(std::memory_order_acquire);
        std::uint32_t slowest = get_slowest_reader_pos();
        return capacity - (w - slowest);
    }

    // 写入数据（一对多广播）
    bool write(const std::uint8_t* src, std::uint32_t len, uint32_t sender_id) {
        const std::uint32_t total_len = sizeof(ShmMessageHeader) + len;

        if (available_write() < total_len) {
            return false; // 空间不足
        }

        std::uint32_t w = write_pos.load(std::memory_order_relaxed);
        uint8_t* data = get_data_ptr();

        // 构造消息头
        ShmMessageHeader header;
        header.length = len;
        header.sender_id = sender_id;
        header.sequence = w; // 使用写位置作为序列号
        header.timestamp = static_cast<uint32_t>(std::time(nullptr));
        header.crc32 = 0; // 简化实现，实际应计算CRC
        header.flags = 0;
        std::memset(header.reserved, 0, sizeof(header.reserved));

        // 写入消息头 - 分两段处理环形缓冲区边界
        std::uint32_t header_first_part = std::min(sizeof(header), capacity - (w & mask));
        std::memcpy(&data[w & mask], &header, header_first_part);

        if (header_first_part < sizeof(header)) {
            std::memcpy(&data[0], reinterpret_cast<const uint8_t*>(&header) + header_first_part,
                       sizeof(header) - header_first_part);
        }

        w += sizeof(header);

        // 写入数据
        if (len > 0) {
            std::uint32_t data_first_part = std::min(len, capacity - (w & mask));
            std::memcpy(&data[w & mask], src, data_first_part);

            if (data_first_part < len) {
                std::memcpy(&data[0], src + data_first_part, len - data_first_part);
            }

            w += len;
        }

        write_pos.store(w, std::memory_order_release);
        return true;
    }

    // 读取数据（特定读者）
    std::optional<std::vector<std::uint8_t>> read(uint32_t reader_id) {
        // 找到读者索引
        int reader_index = -1;
        for (std::uint32_t i = 0; i < max_readers; ++i) {
            if (readers[i].reader_id == reader_id && readers[i].active.load(std::memory_order_acquire)) {
                reader_index = static_cast<int>(i);
                break;
            }
        }

        if (reader_index < 0) {
            return std::nullopt; // 读者未注册
        }

        ReaderState& reader = readers[reader_index];
        std::uint32_t r = reader.read_pos.load(std::memory_order_relaxed);
        std::uint32_t w = write_pos.load(std::memory_order_acquire);

        if (w - r < sizeof(ShmMessageHeader)) {
            return std::nullopt; // 没有完整的消息头
        }

        const uint8_t* data = get_data_ptr();

        // 读取消息头
        ShmMessageHeader header;
        std::uint32_t header_first_part = std::min(sizeof(header), capacity - (r & mask));
        std::memcpy(&header, &data[r & mask], header_first_part);

        if (header_first_part < sizeof(header)) {
            std::memcpy(reinterpret_cast<uint8_t*>(&header) + header_first_part,
                       &data[0], sizeof(header) - header_first_part);
        }

        r += sizeof(header);

        // 检查是否有完整的消息数据
        if (w - r < header.length) {
            return std::nullopt; // 数据不完整
        }

        std::vector<std::uint8_t> result(header.length);

        if (header.length > 0) {
            // 读取消息数据
            std::uint32_t data_first_part = std::min(header.length, capacity - (r & mask));
            std::memcpy(result.data(), &data[r & mask], data_first_part);

            if (data_first_part < header.length) {
                std::memcpy(result.data() + data_first_part, &data[0], header.length - data_first_part);
            }

            r += header.length;
        }

        reader.read_pos.store(r, std::memory_order_release);
        reader.last_access_time = static_cast<uint32_t>(std::time(nullptr));

        return result;
    }

    // 获取读者的可读数据量
    std::uint32_t available_read(uint32_t reader_id) const {
        for (std::uint32_t i = 0; i < max_readers; ++i) {
            if (readers[i].reader_id == reader_id && readers[i].active.load(std::memory_order_acquire)) {
                std::uint32_t w = write_pos.load(std::memory_order_acquire);
                std::uint32_t r = readers[i].read_pos.load(std::memory_order_acquire);
                return w - r;
            }
        }
        return 0;
    }
};

struct ShmControlBlockV2 {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t buffer_size;
    std::uint32_t max_endpoints;
    std::uint32_t max_readers_per_endpoint;
    std::atomic<std::uint32_t> ref_count;

    // 每个端点的互斥锁
    ShmMutex endpoint_mutexes[];
    // 多读者环形缓冲区在互斥锁数组之后

    static constexpr std::uint32_t MAGIC = 0x53484D32; // "SHM2"
    static constexpr std::uint32_t VERSION = 2;

    void init(std::uint32_t buf_size, std::uint32_t max_ep, std::uint32_t max_readers, const char* name_prefix) {
        magic = MAGIC;
        version = VERSION;
        buffer_size = buf_size;
        max_endpoints = max_ep;
        max_readers_per_endpoint = max_readers;
        ref_count.store(0, std::memory_order_relaxed);

        // 初始化互斥锁
        for (std::uint32_t i = 0; i < max_ep; ++i) {
            endpoint_mutexes[i].init(name_prefix, i);
        }

        // 初始化多读者缓冲区
        uint8_t* buffer_start = reinterpret_cast<uint8_t*>(endpoint_mutexes + max_ep);
        for (std::uint32_t i = 0; i < max_ep; ++i) {
            ShmMultiReaderRingBuffer* buffer = get_buffer(i);
            buffer->init(buf_size, max_readers);
        }
    }

    void cleanup() {
        for (std::uint32_t i = 0; i < max_endpoints; ++i) {
            endpoint_mutexes[i].destroy();
        }
    }

    bool is_valid() const {
        return magic == MAGIC && version == VERSION;
    }

    ShmMultiReaderRingBuffer* get_buffer(std::uint32_t endpoint) {
        if (endpoint >= max_endpoints) return nullptr;

        uint8_t* base = reinterpret_cast<uint8_t*>(endpoint_mutexes + max_endpoints);
        size_t buffer_struct_size = sizeof(ShmMultiReaderRingBuffer) +
                                   max_readers_per_endpoint * sizeof(ShmMultiReaderRingBuffer::ReaderState);
        size_t total_buffer_size = buffer_struct_size + buffer_size;

        return reinterpret_cast<ShmMultiReaderRingBuffer*>(base + endpoint * total_buffer_size);
    }

    ShmMutex* get_mutex(std::uint32_t endpoint) {
        if (endpoint >= max_endpoints) return nullptr;
        return &endpoint_mutexes[endpoint];
    }
};

template<std::uint32_t BufferSize = 65536, std::uint32_t MaxEndpoints = 16, std::uint32_t MaxReadersPerEndpoint = 8>
class SharedMemoryLinkV2 : public RawLinkBase<SharedMemoryLinkV2<BufferSize, MaxEndpoints, MaxReadersPerEndpoint>> {
public:
    using control_block_t = ShmControlBlockV2;

    explicit SharedMemoryLinkV2(const std::string& name, std::uint32_t local_endpoint = 0)
        : shm_name_(name), local_endpoint_(local_endpoint), shm_ptr_(nullptr), shm_size_(0), running_(true) {
        shm_size_ = calculate_shm_size();
        create_or_open_shm();
        if (shm_ptr_) {
            register_as_reader();
        }
    }

    ~SharedMemoryLinkV2() {
        close();
    }

    SharedMemoryLinkV2(const SharedMemoryLinkV2&) = delete;
    SharedMemoryLinkV2& operator=(const SharedMemoryLinkV2&) = delete;

    // 修复移动构造函数，正确处理running_状态
    SharedMemoryLinkV2(SharedMemoryLinkV2&& other) noexcept
        : shm_name_(std::move(other.shm_name_))
        , local_endpoint_(other.local_endpoint_)
        , shm_ptr_(other.shm_ptr_)
        , shm_size_(other.shm_size_)
        , running_(other.running_.load(std::memory_order_acquire))
#ifdef _WIN32
        , shm_handle_(other.shm_handle_)
#else
        , shm_fd_(other.shm_fd_)
#endif
    {
        other.shm_ptr_ = nullptr;
        other.shm_size_ = 0;
        other.running_.store(false, std::memory_order_release);
#ifdef _WIN32
        other.shm_handle_ = INVALID_HANDLE_VALUE;
#else
        other.shm_fd_ = -1;
#endif
    }

    SharedMemoryLinkV2& operator=(SharedMemoryLinkV2&& other) noexcept {
        if (this != &other) {
            close();

            shm_name_ = std::move(other.shm_name_);
            local_endpoint_ = other.local_endpoint_;
            shm_ptr_ = other.shm_ptr_;
            shm_size_ = other.shm_size_;
            running_.store(other.running_.load(std::memory_order_acquire), std::memory_order_release);
#ifdef _WIN32
            shm_handle_ = other.shm_handle_;
            other.shm_handle_ = INVALID_HANDLE_VALUE;
#else
            shm_fd_ = other.shm_fd_;
            other.shm_fd_ = -1;
#endif

            other.shm_ptr_ = nullptr;
            other.shm_size_ = 0;
            other.running_.store(false, std::memory_order_release);
        }
        return *this;
    }

    std::size_t mtu_impl() const noexcept {
        return BufferSize - sizeof(ShmMessageHeader);
    }

    // 修改函数签名，使用buffer_view替代std::span
    bool write_impl(std::uint32_t endpoint, traits::buffer_view<const std::uint8_t> data) {
        if (!shm_ptr_ || !running_.load(std::memory_order_acquire)) {
            return false;
        }

        control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
        if (!ctrl->is_valid()) {
            return false;
        }

        ShmMultiReaderRingBuffer* buffer = ctrl->get_buffer(endpoint);
        if (!buffer) {
            return false;
        }

        ShmMutex* mutex = ctrl->get_mutex(endpoint);
        ShmLockGuard lock(mutex);
        if (!lock.is_locked()) {
            return false;
        }

        return buffer->write(data.data(), static_cast<std::uint32_t>(data.size()), local_endpoint_);
    }

    std::optional<std::vector<std::uint8_t>> read_impl() {
        if (!shm_ptr_ || !running_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
        if (!ctrl->is_valid()) {
            return std::nullopt;
        }

        // 尝试从所有端点读取数据
        for (std::uint32_t ep = 0; ep < MaxEndpoints; ++ep) {
            ShmMultiReaderRingBuffer* buffer = ctrl->get_buffer(ep);
            if (buffer) {
                auto result = buffer->read(local_endpoint_);
                if (result) {
                    return result;
                }
            }
        }

        return std::nullopt;
    }

    // 广播到所有端点
    bool broadcast(traits::buffer_view<const std::uint8_t> data) {
        bool success = true;
        for (std::uint32_t ep = 0; ep < MaxEndpoints; ++ep) {
            if (ep != local_endpoint_) {
                success &= write_impl(ep, data);
            }
        }
        return success;
    }

    bool is_connected() const noexcept {
        return shm_ptr_ != nullptr && running_.load(std::memory_order_acquire);
    }

    void close() noexcept {
        running_.store(false, std::memory_order_release);

        if (shm_ptr_) {
            unregister_as_reader();

            control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
            if (ctrl->is_valid()) {
                std::uint32_t ref_count = ctrl->ref_count.fetch_sub(1, std::memory_order_acq_rel);
                if (ref_count == 1) {
                    // 最后一个引用，清理资源
                    ctrl->cleanup();
                }
            }

#ifdef _WIN32
            UnmapViewOfFile(shm_ptr_);
            if (shm_handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(shm_handle_);
                shm_handle_ = INVALID_HANDLE_VALUE;
            }
#else
            munmap(shm_ptr_, shm_size_);
            if (shm_fd_ != -1) {
                ::close(shm_fd_);
                shm_fd_ = -1;
            }
#endif
            shm_ptr_ = nullptr;
        }
    }

    struct Stats {
        std::uint32_t ref_count;
        std::uint32_t available_write;
        std::uint32_t available_read;
        std::uint32_t buffer_utilization;
        std::uint32_t active_readers;
    };

    Stats get_stats() const {
        Stats stats{};
        if (shm_ptr_) {
            control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
            if (ctrl->is_valid()) {
                stats.ref_count = ctrl->ref_count.load(std::memory_order_acquire);

                // 计算缓冲区利用率
                for (std::uint32_t ep = 0; ep < MaxEndpoints; ++ep) {
                    ShmMultiReaderRingBuffer* buffer = ctrl->get_buffer(ep);
                    if (buffer) {
                        stats.available_write += buffer->available_write();
                        stats.available_read += buffer->available_read(local_endpoint_);

                        // 统计活跃读者数量
                        for (std::uint32_t r = 0; r < MaxReadersPerEndpoint; ++r) {
                            if (buffer->readers[r].active.load(std::memory_order_acquire)) {
                                ++stats.active_readers;
                            }
                        }
                    }
                }

                stats.buffer_utilization = (BufferSize * MaxEndpoints - stats.available_write) * 100 / (BufferSize * MaxEndpoints);
            }
        }
        return stats;
    }

    std::uint32_t get_local_endpoint() const noexcept {
        return local_endpoint_;
    }

private:
    std::string shm_name_;
    std::uint32_t local_endpoint_;
    void* shm_ptr_;
    std::size_t shm_size_;
    std::atomic<bool> running_;

#ifdef _WIN32
    HANDLE shm_handle_ = INVALID_HANDLE_VALUE;
#else
    int shm_fd_ = -1;
#endif

    static constexpr std::size_t calculate_shm_size() {
        std::size_t ctrl_size = sizeof(control_block_t) + MaxEndpoints * sizeof(ShmMutex);
        std::size_t buffer_struct_size = sizeof(ShmMultiReaderRingBuffer) +
                                        MaxReadersPerEndpoint * sizeof(ShmMultiReaderRingBuffer::ReaderState);
        std::size_t total_buffer_size = buffer_struct_size + BufferSize;
        return ctrl_size + MaxEndpoints * total_buffer_size;
    }

    void create_or_open_shm() {
#ifdef _WIN32
        shm_handle_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            static_cast<DWORD>(shm_size_ >> 32),
            static_cast<DWORD>(shm_size_ & 0xFFFFFFFF),
            shm_name_.c_str()
        );

        if (shm_handle_ == nullptr) {
            return;
        }

        bool is_new = (GetLastError() != ERROR_ALREADY_EXISTS);

        shm_ptr_ = MapViewOfFile(
            shm_handle_,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            shm_size_
        );

        if (shm_ptr_ == nullptr) {
            CloseHandle(shm_handle_);
            shm_handle_ = INVALID_HANDLE_VALUE;
            return;
        }

        if (is_new) {
            std::memset(shm_ptr_, 0, shm_size_);
            control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
            ctrl->init(BufferSize, MaxEndpoints, MaxReadersPerEndpoint, shm_name_.c_str());
        }

        control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
        ctrl->ref_count.fetch_add(1, std::memory_order_acq_rel);

#else // Linux/Unix
        shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (shm_fd_ == -1) {
            return;
        }

        struct stat shm_stat;
        bool is_new = (fstat(shm_fd_, &shm_stat) == 0 && shm_stat.st_size == 0);

        if (is_new) {
            if (ftruncate(shm_fd_, shm_size_) == -1) {
                ::close(shm_fd_);
                shm_fd_ = -1;
                return;
            }
        }

        shm_ptr_ = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (shm_ptr_ == MAP_FAILED) {
            ::close(shm_fd_);
            shm_fd_ = -1;
            shm_ptr_ = nullptr;
            return;
        }

        if (is_new) {
            std::memset(shm_ptr_, 0, shm_size_);
            control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
            ctrl->init(BufferSize, MaxEndpoints, MaxReadersPerEndpoint, shm_name_.c_str());
        }

        control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
        ctrl->ref_count.fetch_add(1, std::memory_order_acq_rel);
#endif
    }

    void register_as_reader() {
        if (!shm_ptr_) return;

        control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
        if (!ctrl->is_valid()) return;

        for (std::uint32_t ep = 0; ep < MaxEndpoints; ++ep) {
            if (ep != local_endpoint_) {
                ShmMultiReaderRingBuffer* buffer = ctrl->get_buffer(ep);
                if (buffer) {
                    buffer->register_reader(local_endpoint_);
                }
            }
        }
    }

    void unregister_as_reader() {
        if (!shm_ptr_) return;

        control_block_t* ctrl = reinterpret_cast<control_block_t*>(shm_ptr_);
        if (!ctrl->is_valid()) return;

        for (std::uint32_t ep = 0; ep < MaxEndpoints; ++ep) {
            if (ep != local_endpoint_) {
                ShmMultiReaderRingBuffer* buffer = ctrl->get_buffer(ep);
                if (buffer) {
                    buffer->unregister_reader(local_endpoint_);
                }
            }
        }
    }
};

} // namespace comm::link

namespace comm::traits {

template<std::uint32_t B, std::uint32_t E, std::uint32_t R>
struct supports_zero_copy<comm::link::SharedMemoryLinkV2<B, E, R>> : std::true_type {};

template<std::uint32_t B, std::uint32_t E, std::uint32_t R>
struct is_realtime_capable<comm::link::SharedMemoryLinkV2<B, E, R>> : std::true_type {};

template<std::uint32_t B, std::uint32_t E, std::uint32_t R>
struct memory_model<comm::link::SharedMemoryLinkV2<B, E, R>> {
    static constexpr bool is_static = true;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_pool_based = false;
};

} // namespace comm::traits

#endif // COMM_LINK_SHM_HPP