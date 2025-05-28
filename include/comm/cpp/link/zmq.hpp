#pragma once

#ifndef COMM_LINK_ZMQ_HPP
#define COMM_LINK_ZMQ_HPP

#include "../core/traits.hpp"
#include "../core/endpoint.hpp"
#include "raw.hpp"
#include <span>
#include <optional>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <queue>
#include <functional>

// ZMQ头文件
#ifdef COMM_USE_SYSTEM_ZMQ
#include <zmq.h>
#else
// 如果没有系统ZMQ，提供简化的接口定义
extern "C" {
    typedef struct zmq_ctx_t zmq_ctx_t;
    typedef struct zmq_socket_t zmq_socket_t;
    typedef struct zmq_msg_t zmq_msg_t;

    // ZMQ常量
    #define ZMQ_PAIR 0
    #define ZMQ_PUB 1
    #define ZMQ_SUB 2
    #define ZMQ_REQ 3
    #define ZMQ_REP 4
    #define ZMQ_DEALER 5
    #define ZMQ_ROUTER 6
    #define ZMQ_PUSH 7
    #define ZMQ_PULL 8

    #define ZMQ_DONTWAIT 1
    #define ZMQ_SNDMORE 2
    #define ZMQ_RCVMORE 3
    #define ZMQ_SUBSCRIBE 6
    #define ZMQ_UNSUBSCRIBE 7
    #define ZMQ_LINGER 17
    #define ZMQ_RECONNECT_IVL 18
    #define ZMQ_RECONNECT_IVL_MAX 21
    #define ZMQ_SNDHWM 23
    #define ZMQ_RCVHWM 24
    #define ZMQ_IMMEDIATE 39

    // ZMQ函数声明（需要链接libzmq）
    zmq_ctx_t* zmq_ctx_new();
    int zmq_ctx_destroy(zmq_ctx_t* context);
    void* zmq_socket(zmq_ctx_t* context, int type);
    int zmq_close(void* socket);
    int zmq_bind(void* socket, const char* endpoint);
    int zmq_connect(void* socket, const char* endpoint);
    int zmq_send(void* socket, const void* buf, size_t len, int flags);
    int zmq_recv(void* socket, void* buf, size_t len, int flags);
    int zmq_setsockopt(void* socket, int option, const void* optval, size_t optvallen);
    int zmq_getsockopt(void* socket, int option, void* optval, size_t* optvallen);
    int zmq_poll(void* items, int nitems, long timeout);
    int zmq_errno();
    const char* zmq_strerror(int errnum);
}
#endif

namespace comm::link {

// ZMQ错误处理
class ZmqError : public std::runtime_error {
public:
    explicit ZmqError(const std::string& msg) : std::runtime_error("ZMQ Error: " + msg) {}
    explicit ZmqError(const std::string& msg, int error_code)
        : std::runtime_error("ZMQ Error: " + msg + " (code: " + std::to_string(error_code) + ")") {}
};

// ZMQ套接字类型
enum class ZmqSocketType {
    PAIR = ZMQ_PAIR,       // 1对1通信
    PUB = ZMQ_PUB,         // 发布者
    SUB = ZMQ_SUB,         // 订阅者
    REQ = ZMQ_REQ,         // 请求者
    REP = ZMQ_REP,         // 应答者
    DEALER = ZMQ_DEALER,   // 异步请求者
    ROUTER = ZMQ_ROUTER,   // 异步应答者
    PUSH = ZMQ_PUSH,       // 推送者
    PULL = ZMQ_PULL        // 拉取者
};

// ZMQ连接模式
enum class ZmqMode {
    BIND,    // 绑定模式（服务器）
    CONNECT  // 连接模式（客户端）
};

// ZMQ连接状态
enum class ZmqConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR
};

// ZMQ消息队列项
struct ZmqQueueItem {
    std::vector<uint8_t> data;
    std::chrono::steady_clock::time_point timestamp;
    uint32_t retry_count;
    uint32_t endpoint;
};

// ZMQ连接管理器
class ZmqConnectionManager {
public:
    struct ConnectionInfo {
        std::string endpoint;
        ZmqMode mode;
        ZmqSocketType socket_type;
        std::atomic<ZmqConnectionState> state{ZmqConnectionState::DISCONNECTED};
        std::chrono::steady_clock::time_point last_attempt;
        uint32_t retry_count = 0;
        uint32_t max_retries = 5;
        std::chrono::milliseconds retry_interval{1000};
        std::chrono::milliseconds max_retry_interval{30000};
    };

private:
    std::unordered_map<std::string, std::unique_ptr<ConnectionInfo>> connections_;
    std::mutex connections_mutex_;
    std::thread reconnect_thread_;
    std::atomic<bool> running_{false};
    std::condition_variable reconnect_cv_;

public:
    ZmqConnectionManager() {
        running_ = true;
        reconnect_thread_ = std::thread([this] { reconnect_worker(); });
    }

    ~ZmqConnectionManager() {
        running_ = false;
        reconnect_cv_.notify_all();
        if (reconnect_thread_.joinable()) {
            reconnect_thread_.join();
        }
    }

    void add_connection(const std::string& name, const std::string& endpoint,
                       ZmqMode mode, ZmqSocketType socket_type) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto info = std::make_unique<ConnectionInfo>();
        info->endpoint = endpoint;
        info->mode = mode;
        info->socket_type = socket_type;
        connections_[name] = std::move(info);
    }

    void remove_connection(const std::string& name) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(name);
    }

    void set_connection_state(const std::string& name, ZmqConnectionState state) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(name);
        if (it != connections_.end()) {
            it->second->state = state;
            if (state == ZmqConnectionState::ERROR) {
                it->second->last_attempt = std::chrono::steady_clock::now();
                it->second->retry_count++;
            } else if (state == ZmqConnectionState::CONNECTED) {
                it->second->retry_count = 0;
            }
        }
    }

    ZmqConnectionState get_connection_state(const std::string& name) const {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(name);
        return it != connections_.end() ? it->second->state.load() : ZmqConnectionState::DISCONNECTED;
    }

    bool should_retry(const std::string& name) const {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(name);
        if (it == connections_.end()) return false;

        auto& info = *it->second;
        if (info.retry_count >= info.max_retries) return false;

        auto now = std::chrono::steady_clock::now();
        auto interval = std::min(info.retry_interval * (1 << info.retry_count), info.max_retry_interval);
        return now - info.last_attempt >= interval;
    }

private:
    void reconnect_worker() {
        while (running_) {
            std::unique_lock<std::mutex> lock(connections_mutex_);
            reconnect_cv_.wait_for(lock, std::chrono::seconds(1));

            if (!running_) break;

            for (auto& [name, info] : connections_) {
                if (info->state == ZmqConnectionState::ERROR && should_retry(name)) {
                    info->state = ZmqConnectionState::RECONNECTING;
                    // 这里应该触发重连逻辑，但需要在具体的链路实现中处理
                }
            }
        }
    }
};

// 全局连接管理器
static ZmqConnectionManager g_connection_manager;

// ZMQ链路策略基类（增强版）
template<typename Derived>
class ZmqLinkBaseV2 : public RawLinkBase<Derived> {
public:
    ZmqLinkBaseV2(ZmqSocketType socket_type, const std::string& endpoint, ZmqMode mode)
        : socket_type_(socket_type), endpoint_(endpoint), mode_(mode)
        , context_(nullptr), socket_(nullptr), running_(false)
        , send_queue_max_size_(1000), recv_queue_max_size_(1000)
        , connection_name_(generate_connection_name()) {

        init_zmq();
        g_connection_manager.add_connection(connection_name_, endpoint, mode, socket_type);
    }

    virtual ~ZmqLinkBaseV2() {
        close();
        g_connection_manager.remove_connection(connection_name_);
    }

    // 禁用拷贝
    ZmqLinkBaseV2(const ZmqLinkBaseV2&) = delete;
    ZmqLinkBaseV2& operator=(const ZmqLinkBaseV2&) = delete;

    // 移动构造和赋值
    ZmqLinkBaseV2(ZmqLinkBaseV2&& other) noexcept
        : socket_type_(other.socket_type_)
        , endpoint_(std::move(other.endpoint_))
        , mode_(other.mode_)
        , context_(other.context_)
        , socket_(other.socket_)
        , running_(other.running_.load())
        , connection_name_(std::move(other.connection_name_)) {

        other.context_ = nullptr;
        other.socket_ = nullptr;
        other.running_ = false;
    }

    ZmqLinkBaseV2& operator=(ZmqLinkBaseV2&& other) noexcept {
        if (this != &other) {
            close();

            socket_type_ = other.socket_type_;
            endpoint_ = std::move(other.endpoint_);
            mode_ = other.mode_;
            context_ = other.context_;
            socket_ = other.socket_;
            running_ = other.running_.load();
            connection_name_ = std::move(other.connection_name_);

            other.context_ = nullptr;
            other.socket_ = nullptr;
            other.running_ = false;
        }
        return *this;
    }

    bool is_connected() const noexcept {
        return socket_ != nullptr && running_.load() &&
               g_connection_manager.get_connection_state(connection_name_) == ZmqConnectionState::CONNECTED;
    }

    void close() noexcept {
        running_ = false;

        // 停止工作线程
        stop_worker_threads();

        if (socket_) {
            // 设置linger为0，立即关闭
            int linger = 0;
            zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(linger));
            zmq_close(socket_);
            socket_ = nullptr;
        }

        if (context_) {
            zmq_ctx_destroy(context_);
            context_ = nullptr;
        }

        g_connection_manager.set_connection_state(connection_name_, ZmqConnectionState::DISCONNECTED);
    }

    // 获取统计信息
    struct Stats {
        std::uint64_t messages_sent;
        std::uint64_t messages_received;
        std::uint64_t bytes_sent;
        std::uint64_t bytes_received;
        std::uint64_t send_errors;
        std::uint64_t recv_errors;
        std::uint64_t queue_overflows;
        std::uint64_t reconnect_attempts;
        ZmqConnectionState connection_state;
        std::size_t send_queue_size;
        std::size_t recv_queue_size;
    };

    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        Stats stats = stats_;
        stats.connection_state = g_connection_manager.get_connection_state(connection_name_);
        stats.send_queue_size = send_queue_.size();
        stats.recv_queue_size = recv_queue_.size();
        return stats;
    }

    void reset_stats() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = {};
    }

    // 设置队列大小限制
    void set_queue_limits(std::size_t send_limit, std::size_t recv_limit) {
        send_queue_max_size_ = send_limit;
        recv_queue_max_size_ = recv_limit;
    }

    // 异步发送（使用队列）
    bool send_async(std::uint32_t endpoint, std::span<const std::uint8_t> data) {
        if (send_queue_.size() >= send_queue_max_size_) {
            update_send_stats(data.size(), false);
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.queue_overflows;
            return false;
        }

        ZmqQueueItem item;
        item.data.assign(data.begin(), data.end());
        item.timestamp = std::chrono::steady_clock::now();
        item.retry_count = 0;
        item.endpoint = endpoint;

        std::lock_guard<std::mutex> lock(send_queue_mutex_);
        send_queue_.push(std::move(item));
        send_queue_cv_.notify_one();

        return true;
    }

    // 尝试重连
    bool reconnect() {
        if (!running_) return false;

        g_connection_manager.set_connection_state(connection_name_, ZmqConnectionState::RECONNECTING);

        // 关闭现有连接
        if (socket_) {
            zmq_close(socket_);
            socket_ = nullptr;
        }

        try {
            // 重新创建套接字
            socket_ = zmq_socket(context_, static_cast<int>(socket_type_));
            if (!socket_) {
                throw ZmqError("Failed to create socket during reconnect");
            }

            // 设置套接字选项
            configure_socket();

            // 重新连接或绑定
            int result;
            if (mode_ == ZmqMode::BIND) {
                result = zmq_bind(socket_, endpoint_.c_str());
            } else {
                result = zmq_connect(socket_, endpoint_.c_str());
            }

            if (result != 0) {
                throw ZmqError("Failed to reconnect to endpoint: " + endpoint_);
            }

            g_connection_manager.set_connection_state(connection_name_, ZmqConnectionState::CONNECTED);

            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.reconnect_attempts;

            return true;

        } catch (const std::exception&) {
            g_connection_manager.set_connection_state(connection_name_, ZmqConnectionState::ERROR);
            return false;
        }
    }

protected:
    ZmqSocketType socket_type_;
    std::string endpoint_;
    ZmqMode mode_;
    zmq_ctx_t* context_;
    void* socket_;
    std::atomic<bool> running_;

    // 队列管理
    std::queue<ZmqQueueItem> send_queue_;
    std::queue<std::vector<uint8_t>> recv_queue_;
    std::mutex send_queue_mutex_;
    std::mutex recv_queue_mutex_;
    std::condition_variable send_queue_cv_;
    std::condition_variable recv_queue_cv_;
    std::size_t send_queue_max_size_;
    std::size_t recv_queue_max_size_;

    // 工作线程
    std::thread send_worker_;
    std::thread recv_worker_;

    // 统计信息
    mutable std::mutex stats_mutex_;
    mutable Stats stats_{};

    // 连接管理
    std::string connection_name_;

    void init_zmq() {
        context_ = zmq_ctx_new();
        if (!context_) {
            throw ZmqError("Failed to create ZMQ context");
        }

        socket_ = zmq_socket(context_, static_cast<int>(socket_type_));
        if (!socket_) {
            zmq_ctx_destroy(context_);
            context_ = nullptr;
            throw ZmqError("Failed to create ZMQ socket");
        }

        // 配置套接字选项
        configure_socket();

        // 连接或绑定
        int result;
        if (mode_ == ZmqMode::BIND) {
            result = zmq_bind(socket_, endpoint_.c_str());
        } else {
            result = zmq_connect(socket_, endpoint_.c_str());
        }

        if (result != 0) {
            zmq_close(socket_);
            zmq_ctx_destroy(context_);
            socket_ = nullptr;
            context_ = nullptr;
            throw ZmqError("Failed to " + std::string(mode_ == ZmqMode::BIND ? "bind" : "connect") +
                          " to endpoint: " + endpoint_);
        }

        running_ = true;
        g_connection_manager.set_connection_state(connection_name_, ZmqConnectionState::CONNECTED);

        // 启动工作线程
        start_worker_threads();
    }

    void configure_socket() {
        if (!socket_) return;

        // 设置高水位标记
        int hwm = 1000;
        zmq_setsockopt(socket_, ZMQ_SNDHWM, &hwm, sizeof(hwm));
        zmq_setsockopt(socket_, ZMQ_RCVHWM, &hwm, sizeof(hwm));

        // 设置重连间隔
        int reconnect_ivl = 1000; // 1秒
        zmq_setsockopt(socket_, ZMQ_RECONNECT_IVL, &reconnect_ivl, sizeof(reconnect_ivl));

        int max_reconnect_ivl = 30000; // 30秒
        zmq_setsockopt(socket_, ZMQ_RECONNECT_IVL_MAX, &max_reconnect_ivl, sizeof(max_reconnect_ivl));

        // 设置立即连接（不等待对端）
        int immediate = 1;
        zmq_setsockopt(socket_, ZMQ_IMMEDIATE, &immediate, sizeof(immediate));
    }

    void start_worker_threads() {
        send_worker_ = std::thread([this] { send_worker(); });
        recv_worker_ = std::thread([this] { recv_worker(); });
    }

    void stop_worker_threads() {
        if (send_worker_.joinable()) {
            send_queue_cv_.notify_all();
            send_worker_.join();
        }

        if (recv_worker_.joinable()) {
            recv_queue_cv_.notify_all();
            recv_worker_.join();
        }
    }

    void send_worker() {
        while (running_) {
            std::unique_lock<std::mutex> lock(send_queue_mutex_);
            send_queue_cv_.wait(lock, [this] { return !send_queue_.empty() || !running_; });

            if (!running_) break;

            if (send_queue_.empty()) continue;

            auto item = std::move(send_queue_.front());
            send_queue_.pop();
            lock.unlock();

            // 尝试发送
            if (is_connected()) {
                bool success = send_immediate(item.endpoint, item.data);
                if (!success && item.retry_count < 3) {
                    // 重新入队重试
                    item.retry_count++;
                    std::lock_guard<std::mutex> retry_lock(send_queue_mutex_);
                    send_queue_.push(std::move(item));
                }
            } else {
                // 连接断开，尝试重连
                if (g_connection_manager.should_retry(connection_name_)) {
                    reconnect();
                }
            }
        }
    }

    void recv_worker() {
        while (running_) {
            if (!is_connected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            auto data = recv_immediate();
            if (data) {
                std::lock_guard<std::mutex> lock(recv_queue_mutex_);
                if (recv_queue_.size() < recv_queue_max_size_) {
                    recv_queue_.push(std::move(*data));
                    recv_queue_cv_.notify_one();
                } else {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    ++stats_.queue_overflows;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    // 立即发送（不使用队列）
    virtual bool send_immediate(std::uint32_t endpoint, const std::vector<uint8_t>& data) = 0;

    // 立即接收（不使用队列）
    virtual std::optional<std::vector<std::uint8_t>> recv_immediate() = 0;

    void update_send_stats(std::size_t bytes, bool success) const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (success) {
            ++stats_.messages_sent;
            stats_.bytes_sent += bytes;
        } else {
            ++stats_.send_errors;
        }
    }

    void update_recv_stats(std::size_t bytes, bool success) const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (success) {
            ++stats_.messages_received;
            stats_.bytes_received += bytes;
        } else {
            ++stats_.recv_errors;
        }
    }

    std::string generate_connection_name() const {
        static std::atomic<uint64_t> counter{0};
        return "zmq_conn_" + std::to_string(counter.fetch_add(1));
    }
};

// 增强的ZMQ PAIR链路
class ZmqPairLinkV2 : public ZmqLinkBaseV2<ZmqPairLinkV2> {
public:
    explicit ZmqPairLinkV2(const std::string& endpoint, ZmqMode mode = ZmqMode::CONNECT)
        : ZmqLinkBaseV2(ZmqSocketType::PAIR, endpoint, mode) {}

    std::size_t mtu_impl() const noexcept {
        return 65536; // ZMQ默认最大消息大小
    }

    bool write_impl(std::uint32_t endpoint, std::span<const std::uint8_t> data) {
        return send_async(endpoint, data);
    }

    std::optional<std::vector<std::uint8_t>> read_impl() {
        std::unique_lock<std::mutex> lock(recv_queue_mutex_);
        if (recv_queue_.empty()) {
            return std::nullopt;
        }

        auto data = std::move(recv_queue_.front());
        recv_queue_.pop();
        return data;
    }

protected:
    bool send_immediate(std::uint32_t endpoint, const std::vector<uint8_t>& data) override {
        if (!socket_) return false;

        int result = zmq_send(socket_, data.data(), data.size(), ZMQ_DONTWAIT);
        bool success = (result == static_cast<int>(data.size()));
        update_send_stats(data.size(), success);
        return success;
    }

    std::optional<std::vector<std::uint8_t>> recv_immediate() override {
        if (!socket_) return std::nullopt;

        std::vector<std::uint8_t> buffer(65536);
        int result = zmq_recv(socket_, buffer.data(), buffer.size(), ZMQ_DONTWAIT);

        if (result < 0) {
            update_recv_stats(0, false);
            return std::nullopt;
        }

        buffer.resize(result);
        update_recv_stats(result, true);
        return buffer;
    }
};

// 增强的ZMQ发布-订阅链路
class ZmqPubSubLinkV2 : public ZmqLinkBaseV2<ZmqPubSubLinkV2> {
public:
    // 发布者构造函数
    static ZmqPubSubLinkV2 create_publisher(const std::string& endpoint) {
        return ZmqPubSubLinkV2(ZmqSocketType::PUB, endpoint, ZmqMode::BIND);
    }

    // 订阅者构造函数
    static ZmqPubSubLinkV2 create_subscriber(const std::string& endpoint, const std::string& topic = "") {
        ZmqPubSubLinkV2 link(ZmqSocketType::SUB, endpoint, ZmqMode::CONNECT);
        link.subscribe(topic);
        return link;
    }

    std::size_t mtu_impl() const noexcept {
        return 65536;
    }

    bool write_impl(std::uint32_t endpoint, std::span<const std::uint8_t> data) {
        if (socket_type_ != ZmqSocketType::PUB) {
            return false;
        }
        return send_async(endpoint, data);
    }

    std::optional<std::vector<std::uint8_t>> read_impl() {
        if (socket_type_ != ZmqSocketType::SUB) {
            return std::nullopt;
        }

        std::unique_lock<std::mutex> lock(recv_queue_mutex_);
        if (recv_queue_.empty()) {
            return std::nullopt;
        }

        auto data = std::move(recv_queue_.front());
        recv_queue_.pop();
        return data;
    }

    // 订阅主题
    void subscribe(const std::string& topic) {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        if (socket_ && socket_type_ == ZmqSocketType::SUB) {
            zmq_setsockopt(socket_, ZMQ_SUBSCRIBE, topic.c_str(), topic.size());
            subscriptions_.insert(topic);
        }
    }

    // 取消订阅主题
    void unsubscribe(const std::string& topic) {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        if (socket_ && socket_type_ == ZmqSocketType::SUB) {
            zmq_setsockopt(socket_, ZMQ_UNSUBSCRIBE, topic.c_str(), topic.size());
            subscriptions_.erase(topic);
        }
    }

    // 获取订阅列表
    std::vector<std::string> get_subscriptions() const {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        return std::vector<std::string>(subscriptions_.begin(), subscriptions_.end());
    }

protected:
    bool send_immediate(std::uint32_t endpoint, const std::vector<uint8_t>& data) override {
        if (!socket_ || socket_type_ != ZmqSocketType::PUB) {
            return false;
        }

        // 发布消息，主题为端点ID
        std::string topic = std::to_string(endpoint);

        // 发送主题
        int result1 = zmq_send(socket_, topic.c_str(), topic.size(), ZMQ_SNDMORE | ZMQ_DONTWAIT);
        if (result1 < 0) {
            update_send_stats(data.size(), false);
            return false;
        }

        // 发送数据
        int result2 = zmq_send(socket_, data.data(), data.size(), ZMQ_DONTWAIT);
        bool success = (result2 == static_cast<int>(data.size()));
        update_send_stats(data.size(), success);
        return success;
    }

    std::optional<std::vector<std::uint8_t>> recv_immediate() override {
        if (!socket_ || socket_type_ != ZmqSocketType::SUB) {
            return std::nullopt;
        }

        // 接收主题
        std::vector<std::uint8_t> topic_buffer(256);
        int topic_result = zmq_recv(socket_, topic_buffer.data(), topic_buffer.size(), ZMQ_DONTWAIT);
        if (topic_result < 0) {
            update_recv_stats(0, false);
            return std::nullopt;
        }

        // 检查是否还有更多部分
        int more = 0;
        size_t more_size = sizeof(more);
        zmq_getsockopt(socket_, ZMQ_RCVMORE, &more, &more_size);

        if (!more) {
            update_recv_stats(0, false);
            return std::nullopt;
        }

        // 接收数据
        std::vector<std::uint8_t> data_buffer(65536);
        int data_result = zmq_recv(socket_, data_buffer.data(), data_buffer.size(), ZMQ_DONTWAIT);
        if (data_result < 0) {
            update_recv_stats(0, false);
            return std::nullopt;
        }

        data_buffer.resize(data_result);
        update_recv_stats(data_result, true);
        return data_buffer;
    }

private:
    ZmqPubSubLinkV2(ZmqSocketType type, const std::string& endpoint, ZmqMode mode)
        : ZmqLinkBaseV2(type, endpoint, mode) {}

    std::unordered_set<std::string> subscriptions_;
    mutable std::mutex subscriptions_mutex_;
};

// 便利函数
namespace zmq {
    // 创建TCP连接
    inline std::string tcp_endpoint(const std::string& host, std::uint16_t port) {
        return "tcp://" + host + ":" + std::to_string(port);
    }

    // 创建IPC连接
    inline std::string ipc_endpoint(const std::string& path) {
        return "ipc://" + path;
    }

    // 创建进程内连接
    inline std::string inproc_endpoint(const std::string& name) {
        return "inproc://" + name;
    }

    // 创建WebSocket连接
    inline std::string ws_endpoint(const std::string& host, std::uint16_t port, const std::string& path = "/") {
        return "ws://" + host + ":" + std::to_string(port) + path;
    }
}

// 保持向后兼容的别名
using ZmqPairLink = ZmqPairLinkV2;
using ZmqPubSubLink = ZmqPubSubLinkV2;

} // namespace comm::link

// 特化traits
namespace comm::traits {

template<>
struct is_realtime_capable<comm::link::ZmqPairLinkV2> : std::false_type {};

template<>
struct is_realtime_capable<comm::link::ZmqPubSubLinkV2> : std::false_type {};

template<>
struct memory_model<comm::link::ZmqPairLinkV2> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = true; // 使用内部队列池
};

template<>
struct memory_model<comm::link::ZmqPubSubLinkV2> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = true;
};

} // namespace comm::traits

#endif // COMM_LINK_ZMQ_HPP