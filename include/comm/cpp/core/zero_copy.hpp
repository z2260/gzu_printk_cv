#pragma once

#ifndef COMM_CORE_ZERO_COPY_HPP
#define COMM_CORE_ZERO_COPY_HPP

#include "traits.hpp"
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <cstring>

namespace comm::core {

// 零拷贝缓冲区基类
class ZeroCopyBuffer {
public:
    virtual ~ZeroCopyBuffer() = default;

    virtual std::uint8_t* data() noexcept = 0;
    virtual const std::uint8_t* data() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual std::size_t capacity() const noexcept = 0;

    virtual void resize(std::size_t new_size) = 0;
    virtual bool is_shared() const noexcept = 0;
    virtual std::size_t ref_count() const noexcept = 0;

    // 创建一个新的引用（增加引用计数）
    virtual std::shared_ptr<ZeroCopyBuffer> share() = 0;

    // 如果缓冲区被共享，创建一个独立的副本
    virtual std::shared_ptr<ZeroCopyBuffer> clone_if_shared() = 0;
};

// 引用计数的缓冲区实现
class RefCountedBuffer : public ZeroCopyBuffer {
public:
    explicit RefCountedBuffer(std::size_t size)
        : data_(size), size_(size), ref_count_(1) {}

    RefCountedBuffer(const std::uint8_t* data, std::size_t size)
        : data_(data, data + size), size_(size), ref_count_(1) {}

    std::uint8_t* data() noexcept override {
        return data_.data();
    }

    const std::uint8_t* data() const noexcept override {
        return data_.data();
    }

    std::size_t size() const noexcept override {
        return size_;
    }

    std::size_t capacity() const noexcept override {
        return data_.capacity();
    }

    void resize(std::size_t new_size) override {
        if (is_shared()) {
            throw std::runtime_error("Cannot resize shared buffer");
        }
        data_.resize(new_size);
        size_ = new_size;
    }

    bool is_shared() const noexcept override {
        return ref_count_.load() > 1;
    }

    std::size_t ref_count() const noexcept override {
        return ref_count_.load();
    }

    std::shared_ptr<ZeroCopyBuffer> share() override {
        ref_count_.fetch_add(1);
        return std::shared_ptr<ZeroCopyBuffer>(this, [](ZeroCopyBuffer* ptr) {
            auto* self = static_cast<RefCountedBuffer*>(ptr);
            if (self->ref_count_.fetch_sub(1) == 1) {
                delete self;
            }
        });
    }

    std::shared_ptr<ZeroCopyBuffer> clone_if_shared() override {
        if (!is_shared()) {
            return share();
        }

        return std::make_shared<RefCountedBuffer>(data_.data(), size_);
    }

private:
    std::vector<std::uint8_t> data_;
    std::size_t size_;
    std::atomic<std::size_t> ref_count_;
};

// 内存池管理器
class MemoryPool {
public:
    static constexpr std::size_t DEFAULT_BLOCK_SIZE = 4096;
    static constexpr std::size_t MAX_POOL_SIZE = 64; // 最多缓存64个块

    explicit MemoryPool(std::size_t block_size = DEFAULT_BLOCK_SIZE)
        : block_size_(block_size) {}

    ~MemoryPool() {
        clear();
    }

    std::shared_ptr<ZeroCopyBuffer> allocate(std::size_t size) {
        if (size <= block_size_) {
            return allocate_from_pool();
        } else {
            // 大块内存直接分配
            return std::make_shared<RefCountedBuffer>(size);
        }
    }

    std::shared_ptr<ZeroCopyBuffer> allocate(const std::uint8_t* data, std::size_t size) {
        auto buffer = allocate(size);
        if (buffer && size > 0) {
            std::memcpy(buffer->data(), data, size);
            buffer->resize(size);
        }
        return buffer;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        free_blocks_.clear();
        stats_.pool_clears++;
    }

    struct PoolStats {
        std::size_t allocations = 0;
        std::size_t deallocations = 0;
        std::size_t pool_hits = 0;
        std::size_t pool_misses = 0;
        std::size_t pool_clears = 0;
        std::size_t current_pool_size = 0;
        std::size_t peak_pool_size = 0;
    };

    PoolStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto stats = stats_;
        stats.current_pool_size = free_blocks_.size();
        return stats;
    }

    void reset_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = PoolStats{};
    }

private:
    class PooledBuffer : public RefCountedBuffer {
    public:
        PooledBuffer(std::size_t size, MemoryPool* pool)
            : RefCountedBuffer(size), pool_(pool) {}

        ~PooledBuffer() {
            if (pool_) {
                pool_->return_to_pool(std::move(static_cast<RefCountedBuffer&>(*this)));
            }
        }

    private:
        MemoryPool* pool_;
    };

    std::shared_ptr<ZeroCopyBuffer> allocate_from_pool() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!free_blocks_.empty()) {
            auto buffer = std::move(free_blocks_.back());
            free_blocks_.pop_back();
            buffer->resize(0); // 重置大小
            stats_.pool_hits++;
            stats_.allocations++;
            return buffer;
        } else {
            stats_.pool_misses++;
            stats_.allocations++;
            return std::make_shared<PooledBuffer>(block_size_, this);
        }
    }

    void return_to_pool(RefCountedBuffer&& buffer) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (free_blocks_.size() < MAX_POOL_SIZE) {
            // 重置缓冲区状态
            auto shared_buffer = std::make_shared<RefCountedBuffer>(std::move(buffer));
            free_blocks_.push_back(shared_buffer);

            stats_.peak_pool_size = std::max(stats_.peak_pool_size, free_blocks_.size());
        }

        stats_.deallocations++;
    }

    std::size_t block_size_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<RefCountedBuffer>> free_blocks_;
    mutable PoolStats stats_;
};

// 零拷贝缓冲区视图
class ZeroCopyView {
public:
    ZeroCopyView() = default;

    explicit ZeroCopyView(std::shared_ptr<ZeroCopyBuffer> buffer)
        : buffer_(std::move(buffer)), offset_(0), size_(buffer_ ? buffer_->size() : 0) {}

    ZeroCopyView(std::shared_ptr<ZeroCopyBuffer> buffer, std::size_t offset, std::size_t size)
        : buffer_(std::move(buffer)), offset_(offset), size_(size) {
        if (buffer_ && offset_ + size_ > buffer_->size()) {
            throw std::out_of_range("View exceeds buffer bounds");
        }
    }

    const std::uint8_t* data() const noexcept {
        return buffer_ ? buffer_->data() + offset_ : nullptr;
    }

    std::size_t size() const noexcept {
        return size_;
    }

    bool empty() const noexcept {
        return size_ == 0 || !buffer_;
    }

    // 创建子视图
    ZeroCopyView subview(std::size_t offset, std::size_t size) const {
        if (offset + size > size_) {
            throw std::out_of_range("Subview exceeds view bounds");
        }
        return ZeroCopyView(buffer_, offset_ + offset, size);
    }

    // 获取底层缓冲区
    std::shared_ptr<ZeroCopyBuffer> buffer() const {
        return buffer_;
    }

    // 检查是否与另一个视图共享缓冲区
    bool shares_buffer_with(const ZeroCopyView& other) const {
        return buffer_ && buffer_ == other.buffer_;
    }

    // 转换为traits::buffer_view
    traits::buffer_view<const std::uint8_t> to_buffer_view() const {
        return traits::buffer_view<const std::uint8_t>(data(), size());
    }

    // 迭代器支持
    const std::uint8_t* begin() const noexcept { return data(); }
    const std::uint8_t* end() const noexcept { return data() + size(); }

private:
    std::shared_ptr<ZeroCopyBuffer> buffer_;
    std::size_t offset_ = 0;
    std::size_t size_ = 0;
};

// 零拷贝构建器
class ZeroCopyBuilder {
public:
    explicit ZeroCopyBuilder(MemoryPool& pool) : pool_(pool) {}

    // 从现有数据创建
    ZeroCopyView from_data(const std::uint8_t* data, std::size_t size) {
        auto buffer = pool_.allocate(data, size);
        return ZeroCopyView(buffer);
    }

    // 从vector创建
    ZeroCopyView from_vector(const std::vector<std::uint8_t>& vec) {
        return from_data(vec.data(), vec.size());
    }

    // 分配新缓冲区
    ZeroCopyView allocate(std::size_t size) {
        auto buffer = pool_.allocate(size);
        return ZeroCopyView(buffer);
    }

    // 连接多个视图
    ZeroCopyView concat(const std::vector<ZeroCopyView>& views) {
        std::size_t total_size = 0;
        for (const auto& view : views) {
            total_size += view.size();
        }

        auto buffer = pool_.allocate(total_size);
        std::uint8_t* dest = buffer->data();

        for (const auto& view : views) {
            std::memcpy(dest, view.data(), view.size());
            dest += view.size();
        }

        return ZeroCopyView(buffer);
    }

    // 复制视图（如果需要）
    ZeroCopyView copy_if_shared(const ZeroCopyView& view) {
        auto buffer = view.buffer();
        if (!buffer || !buffer->is_shared()) {
            return view;
        }

        return from_data(view.data(), view.size());
    }

private:
    MemoryPool& pool_;
};

// 全局内存池实例
class GlobalMemoryPool {
public:
    static MemoryPool& instance() {
        static MemoryPool pool;
        return pool;
    }

    static ZeroCopyBuilder builder() {
        return ZeroCopyBuilder(instance());
    }
};

// 便利函数
inline ZeroCopyView make_zero_copy_view(const std::uint8_t* data, std::size_t size) {
    return GlobalMemoryPool::builder().from_data(data, size);
}

inline ZeroCopyView make_zero_copy_view(const std::vector<std::uint8_t>& vec) {
    return GlobalMemoryPool::builder().from_vector(vec);
}

inline ZeroCopyView allocate_zero_copy(std::size_t size) {
    return GlobalMemoryPool::builder().allocate(size);
}

// 零拷贝传输适配器
template<typename Transport>
class ZeroCopyTransport {
public:
    template<typename... Args>
    explicit ZeroCopyTransport(Args&&... args) : transport_(std::forward<Args>(args)...) {}

    // 发送零拷贝视图
    std::optional<ZeroCopyView> send(const ZeroCopyView& view) {
        auto buffer_view = view.to_buffer_view();
        auto result = transport_.send(buffer_view);

        if (result) {
            return make_zero_copy_view(*result);
        }
        return std::nullopt;
    }

    // 接收为零拷贝视图
    std::optional<ZeroCopyView> receive() {
        auto result = transport_.receive();
        if (result) {
            return make_zero_copy_view(*result);
        }
        return std::nullopt;
    }

    // 传统接口兼容性
    std::optional<std::vector<std::uint8_t>> send(traits::buffer_view<const std::uint8_t> data) {
        return transport_.send(data);
    }

    std::optional<std::vector<std::uint8_t>> receive_vector() {
        return transport_.receive();
    }

    bool connect() { return transport_.connect(); }
    void disconnect() { transport_.disconnect(); }
    bool is_connected() const { return transport_.is_connected(); }

    Transport& underlying() { return transport_; }
    const Transport& underlying() const { return transport_; }

private:
    Transport transport_;
};

} // namespace comm::core

namespace comm::traits {

template<>
struct memory_model<comm::core::ZeroCopyView> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = true;
};

template<typename Transport>
struct memory_model<comm::core::ZeroCopyTransport<Transport>> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = true;
};

template<typename Transport>
struct is_realtime_capable<comm::core::ZeroCopyTransport<Transport>> :
    is_realtime_capable<Transport> {};

} // namespace comm::traits

#endif // COMM_CORE_ZERO_COPY_HPP