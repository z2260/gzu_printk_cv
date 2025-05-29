#pragma once

#ifndef COMM_SERVICE_NONE_HPP
#define COMM_SERVICE_NONE_HPP

#include "../core/traits.hpp"
#include "log/log_accessor.hpp"
#include <functional>
#include <type_traits>
#include <exception>
#include <unordered_map>
#include <typeindex>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <array>
#include <sstream>

namespace comm::service {

template<typename Derived>
class ServiceBase : public logger::LogAccessor<Derived> {
public:
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }
    const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

    template<typename Message, typename Handler>
    void handle(Message&& msg, Handler&& handler) {
        MTRACE("ServiceBase::handle called for message type: {}", typeid(Message).name());
        derived().template handle_impl(std::forward<Message>(msg), std::forward<Handler>(handler));
    }

protected:
    ServiceBase() {
        MDEBUG("ServiceBase constructed: {}", typeid(Derived).name());
    }

    ~ServiceBase() {
        MDEBUG("ServiceBase destructed: {}", typeid(Derived).name());
    }

    // 禁止拷贝和移动，确保服务的唯一性
    ServiceBase(const ServiceBase&) = delete;
    ServiceBase& operator=(const ServiceBase&) = delete;
    ServiceBase(ServiceBase&&) = delete;
    ServiceBase& operator=(ServiceBase&&) = delete;
};

class None : public ServiceBase<None> {
public:
    None() {
        MINFO("None service initialized - direct message passing mode");
    }

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        MTRACE("None::handle_impl processing message directly");
        try {
            handler(std::forward<Message>(msg));
            MTRACE("None::handle_impl message processed successfully");
        } catch (const std::exception& e) {
            MERROR("None::handle_impl failed to process message: {}", e.what());
            throw;
        } catch (...) {
            MERROR("None::handle_impl failed to process message: unknown exception");
            throw;
        }
    }
};

class SimpleRouter : public ServiceBase<SimpleRouter> {
private:
    using HandlerFunc = std::function<void(const void*)>;
    std::unordered_map<std::type_index, HandlerFunc> handlers_;
    mutable std::mutex handlers_mutex_;

public:
    SimpleRouter() {
        MINFO("SimpleRouter service initialized");
    }

    ~SimpleRouter() {
        MINFO("SimpleRouter service shutting down with {} registered handlers", handler_count());
    }

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        const auto message_type = typeid(std::decay_t<Message>).name();
        MTRACE("SimpleRouter::handle_impl processing message type: {}", message_type);

        std::lock_guard<std::mutex> lock(handlers_mutex_);

        auto type_idx = std::type_index(typeid(std::decay_t<Message>));
        auto it = handlers_.find(type_idx);

        if (it != handlers_.end()) {
            MDEBUG("SimpleRouter using registered handler for type: {}", message_type);
            try {
                it->second(static_cast<const void*>(&msg));
                MTRACE("SimpleRouter registered handler completed successfully");
            } catch (const std::exception& e) {
                MERROR("SimpleRouter registered handler failed for type {}: {}", message_type, e.what());
                throw;
            } catch (...) {
                MERROR("SimpleRouter registered handler failed for type {}: unknown exception", message_type);
                throw;
            }
        } else {
            MDEBUG("SimpleRouter using default handler for type: {}", message_type);
            try {
                handler(std::forward<Message>(msg));
                MTRACE("SimpleRouter default handler completed successfully");
            } catch (const std::exception& e) {
                MERROR("SimpleRouter default handler failed for type {}: {}", message_type, e.what());
                throw;
            } catch (...) {
                MERROR("SimpleRouter default handler failed for type {}: unknown exception", message_type);
                throw;
            }
        }
    }

    template<typename MessageType, typename SpecificHandler>
    void register_handler(SpecificHandler&& handler) {
        const auto message_type = typeid(MessageType).name();
        MINFO("SimpleRouter registering handler for type: {}", message_type);

        std::lock_guard<std::mutex> lock(handlers_mutex_);

        auto type_idx = std::type_index(typeid(MessageType));
        handlers_[type_idx] = [captured_handler = std::forward<SpecificHandler>(handler), message_type, this]
                             (const void* msg_ptr) {
            try {
                const auto* typed_msg = static_cast<const MessageType*>(msg_ptr);
                captured_handler(*typed_msg);
                MTRACE("SimpleRouter specific handler executed successfully for type: {}", message_type);
            } catch (const std::exception& e) {
                MERROR("SimpleRouter specific handler failed for type {}: {}", message_type, e.what());
                throw;
            } catch (...) {
                MERROR("SimpleRouter specific handler failed for type {}: unknown exception", message_type);
                throw;
            }
        };

        MDEBUG("SimpleRouter handler registered successfully for type: {}", message_type);
    }

    template<typename MessageType>
    void unregister_handler() {
        const auto message_type = typeid(MessageType).name();
        MINFO("SimpleRouter unregistering handler for type: {}", message_type);

        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto type_idx = std::type_index(typeid(MessageType));
        auto count = handlers_.erase(type_idx);

        if (count > 0) {
            MDEBUG("SimpleRouter handler unregistered successfully for type: {}", message_type);
        } else {
            MWARN("SimpleRouter no handler found to unregister for type: {}", message_type);
        }
    }

    void clear_handlers() {
        MINFO("SimpleRouter clearing all handlers");
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto count = handlers_.size();
        handlers_.clear();
        MDEBUG("SimpleRouter cleared {} handlers", count);
    }

    std::size_t handler_count() const {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        return handlers_.size();
    }
};

template<std::size_t QueueSize = 256>
class Async : public ServiceBase<Async<QueueSize>> {
private:
    struct MessageEntry {
        std::function<void()> processor;
        std::chrono::steady_clock::time_point timestamp;

        MessageEntry() = default;

        template<typename Func>
        MessageEntry(Func&& func)
            : processor(std::forward<Func>(func))
            , timestamp(std::chrono::steady_clock::now()) {}
    };

    std::array<MessageEntry, QueueSize> queue_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::size_t> size_{0};

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;

public:
    Async() {
        MINFO("Async service initializing with queue size: {}", QueueSize);
        start_worker();
    }

    ~Async() {
        MINFO("Async service shutting down, processing remaining {} messages", queue_size());
        stop_worker();
    }

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        const auto message_type = typeid(std::decay_t<Message>).name();
        MTRACE("Async::handle_impl enqueuing message type: {}", message_type);

        auto processor = [msg = std::forward<Message>(msg),
                         handler = std::forward<Handler>(handler),
                         message_type, this]() mutable {
            try {
                MTRACE("Async worker processing message type: {}", message_type);
                handler(std::move(msg));
                MTRACE("Async worker completed message type: {}", message_type);
            } catch (const std::exception& e) {
                MERROR("Async worker failed to process message type {}: {}", message_type, e.what());
            } catch (...) {
                MERROR("Async worker failed to process message type {}: unknown exception", message_type);
            }
        };

        // 尝试入队
        if (!enqueue(std::move(processor))) {
            MERROR("Async message queue is full (size: {}), cannot enqueue message type: {}",
                   QueueSize, message_type);
            throw std::runtime_error("Message queue is full");
        }

        MTRACE("Async message enqueued successfully, queue size: {}", queue_size());
    }

    void process_queue() {
        MessageEntry entry;
        std::size_t processed_count = 0;

        while (dequeue(entry)) {
            try {
                if (entry.processor) {
                    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - entry.timestamp);

                    if (age.count() > 1000) { // 超过1秒的消息
                        MWARN("Async processing aged message ({}ms old)", age.count());
                    }

                    entry.processor();
                    ++processed_count;
                }
            } catch (const std::exception& e) {
                MERROR("Async queue processor caught exception: {}", e.what());
            } catch (...) {
                MERROR("Async queue processor caught unknown exception");
            }
        }

        if (processed_count > 0) {
            MTRACE("Async processed {} messages from queue", processed_count);
        }
    }

    std::size_t queue_size() const noexcept {
        return size_.load(std::memory_order_acquire);
    }

    bool is_queue_full() const noexcept {
        return queue_size() >= QueueSize;
    }

    bool is_queue_empty() const noexcept {
        return queue_size() == 0;
    }

    void start_worker() {
        if (!running_.exchange(true)) {
            MINFO("Async starting worker thread");
            worker_thread_ = std::thread([this] {
                MDEBUG("Async worker thread started");
                worker_loop();
                MDEBUG("Async worker thread finished");
            });
        } else {
            MWARN("Async worker thread already running");
        }
    }

    void stop_worker() {
        if (running_.exchange(false)) {
            MINFO("Async stopping worker thread");
            queue_cv_.notify_all();
            if (worker_thread_.joinable()) {
                worker_thread_.join();
                MDEBUG("Async worker thread joined successfully");
            }
        }
    }

    double queue_utilization() const noexcept {
        auto utilization = static_cast<double>(queue_size()) / QueueSize;
        if (utilization > 0.8) {
            MWARN("Async queue utilization high: {:.1f}%", utilization * 100);
        }
        return utilization;
    }

private:
    bool enqueue(std::function<void()>&& processor) {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (size_.load(std::memory_order_acquire) >= QueueSize) {
            return false;
        }

        std::size_t current_tail = tail_.load(std::memory_order_relaxed);
        queue_[current_tail] = MessageEntry(std::move(processor));

        tail_.store((current_tail + 1) % QueueSize, std::memory_order_release);
        size_.fetch_add(1, std::memory_order_acq_rel);

        queue_cv_.notify_one();
        return true;
    }

    bool dequeue(MessageEntry& entry) {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (size_.load(std::memory_order_acquire) == 0) {
            return false;
        }

        std::size_t current_head = head_.load(std::memory_order_relaxed);
        entry = std::move(queue_[current_head]);

        head_.store((current_head + 1) % QueueSize, std::memory_order_release);
        size_.fetch_sub(1, std::memory_order_acq_rel);

        return true;
    }

    void worker_loop() {
        auto last_stats_time = std::chrono::steady_clock::now();
        std::size_t total_processed = 0;

        while (running_.load()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !running_.load() || size_.load(std::memory_order_acquire) > 0;
            });

            if (!running_.load()) {
                break;
            }

            lock.unlock();

            auto before_size = queue_size();
            process_queue();
            auto after_size = queue_size();
            auto processed = before_size - after_size;
            total_processed += processed;

            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time > std::chrono::seconds(30)) {
                MINFO("Async worker stats: processed={}, queue_size={}, utilization={:.1f}%",
                      total_processed, queue_size(), queue_utilization() * 100);
                last_stats_time = now;
            }
        }

        MINFO("Async worker processed {} messages before shutdown", total_processed);
        process_queue();
    }
};

template<typename BaseService>
class Statistics : public ServiceBase<Statistics<BaseService>> {
public:
    template<typename... Args>
    explicit Statistics(Args&&... args) : base_(std::forward<Args>(args)...) {
        MINFO("Statistics service initialized for base service: {}", typeid(BaseService).name());
    }

    ~Statistics() {
        auto stats = get_stats();
        MINFO("Statistics service shutdown - Total: {}, Processed: {}, Errors: {}, Avg time: {:.2f}μs",
              stats.total_messages, stats.processed_messages, stats.error_messages,
              stats.average_processing_time_us);
    }

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        const auto message_type = typeid(std::decay_t<Message>).name();
        auto start_time = std::chrono::high_resolution_clock::now();
        ++total_messages_;

        MTRACE("Statistics processing message type: {} (total count: {})",
               message_type, total_messages_.load());

        try {
            base_.template handle(std::forward<Message>(msg), [&](auto&& processed_msg) {
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

                auto duration_us = duration.count();
                total_processing_time_ += duration_us;

                auto current_max = max_processing_time_.load();
                while (duration_us > current_max &&
                       !max_processing_time_.compare_exchange_weak(current_max, duration_us)) {
                }

                ++processed_messages_;

                if (duration_us > 10000) {
                    MWARN("Statistics slow message processing: {}μs for type: {}",
                          duration_us, message_type);
                }

                MTRACE("Statistics message processed successfully in {}μs, type: {}",
                       duration_us, message_type);

                handler(std::forward<decltype(processed_msg)>(processed_msg));
            });
        } catch (const std::exception& e) {
            ++error_messages_;
            MERROR("Statistics message processing failed for type {}: {}", message_type, e.what());
            throw;
        } catch (...) {
            ++error_messages_;
            MERROR("Statistics message processing failed for type {}: unknown exception", message_type);
            throw;
        }
    }

    struct Stats {
        std::uint64_t total_messages;
        std::uint64_t processed_messages;
        std::uint64_t error_messages;
        std::uint64_t total_processing_time_us;
        std::uint64_t max_processing_time_us;
        double average_processing_time_us;
        double error_rate;
    };

    Stats get_stats() const noexcept {
        auto total = total_messages_.load();
        auto processed = processed_messages_.load();
        auto errors = error_messages_.load();
        auto total_time = total_processing_time_.load();
        auto max_time = max_processing_time_.load();

        double avg_time = processed > 0 ? static_cast<double>(total_time) / processed : 0.0;
        double err_rate = total > 0 ? static_cast<double>(errors) / total : 0.0;

        return {total, processed, errors, total_time, max_time, avg_time, err_rate};
    }

    void reset_stats() noexcept {
        MINFO("Statistics resetting all counters");
        total_messages_ = 0;
        processed_messages_ = 0;
        error_messages_ = 0;
        total_processing_time_ = 0;
        max_processing_time_ = 0;
    }

    void log_stats() const {
        auto stats = get_stats();
        MINFO("Statistics summary - Total: {}, Processed: {}, Errors: {}, "
              "Error rate: {:.2f}%, Avg time: {:.2f}μs, Max time: {}μs",
              stats.total_messages, stats.processed_messages, stats.error_messages,
              stats.error_rate * 100, stats.average_processing_time_us, stats.max_processing_time_us);
    }

    BaseService& base() noexcept { return base_; }
    const BaseService& base() const noexcept { return base_; }

private:
    BaseService base_;
    std::atomic<std::uint64_t> total_messages_{0};
    std::atomic<std::uint64_t> processed_messages_{0};
    std::atomic<std::uint64_t> error_messages_{0};
    std::atomic<std::uint64_t> total_processing_time_{0};
    std::atomic<std::uint64_t> max_processing_time_{0};
};

template<typename BaseService, typename FilterPredicate>
class Filter : public ServiceBase<Filter<BaseService, FilterPredicate>> {
public:
    template<typename... Args>
    explicit Filter(FilterPredicate pred, Args&&... args)
        : predicate_(std::move(pred)), base_(std::forward<Args>(args)...) {
        MINFO("Filter service initialized for base service: {}", typeid(BaseService).name());
    }

    ~Filter() {
        auto stats = get_filter_stats();
        MINFO("Filter service shutdown - Total: {}, Accepted: {}, Filtered: {}, "
              "Acceptance rate: {:.2f}%",
              stats.total_messages, stats.accepted_messages, stats.filtered_messages,
              stats.acceptance_rate * 100);
    }

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        const auto message_type = typeid(std::decay_t<Message>).name();
        ++total_messages_;

        MTRACE("Filter evaluating message type: {} (total count: {})",
               message_type, total_messages_.load());

        try {
            if (predicate_(msg)) {
                ++accepted_messages_;
                MTRACE("Filter accepted message type: {}", message_type);
                base_.template handle(std::forward<Message>(msg), std::forward<Handler>(handler));
            } else {
                ++filtered_messages_;
                MDEBUG("Filter rejected message type: {}", message_type);
                if (on_filtered_) {
                    try {
                        on_filtered_(msg);
                    } catch (const std::exception& e) {
                        MWARN("Filter callback failed for type {}: {}", message_type, e.what());
                    } catch (...) {
                        MWARN("Filter callback failed for type {}: unknown exception", message_type);
                    }
                }
            }
        } catch (const std::exception& e) {
            MERROR("Filter predicate evaluation failed for type {}: {}", message_type, e.what());
            throw;
        } catch (...) {
            MERROR("Filter predicate evaluation failed for type {}: unknown exception", message_type);
            throw;
        }
    }

    template<typename Callback>
    void set_filtered_callback(Callback&& callback) {
        MINFO("Filter setting filtered callback");
        on_filtered_ = std::forward<Callback>(callback);
    }

    struct FilterStats {
        std::uint64_t total_messages;
        std::uint64_t accepted_messages;
        std::uint64_t filtered_messages;
        double acceptance_rate;
    };

    FilterStats get_filter_stats() const noexcept {
        auto total = total_messages_.load();
        auto accepted = accepted_messages_.load();
        auto filtered = filtered_messages_.load();
        double rate = total > 0 ? static_cast<double>(accepted) / total : 0.0;

        return {total, accepted, filtered, rate};
    }

    void reset_filter_stats() noexcept {
        MINFO("Filter resetting statistics");
        total_messages_ = 0;
        accepted_messages_ = 0;
        filtered_messages_ = 0;
    }

    void log_filter_stats() const {
        auto stats = get_filter_stats();
        MINFO("Filter stats - Total: {}, Accepted: {}, Filtered: {}, Acceptance rate: {:.2f}%",
              stats.total_messages, stats.accepted_messages, stats.filtered_messages,
              stats.acceptance_rate * 100);
    }

    BaseService& base() noexcept { return base_; }
    const BaseService& base() const noexcept { return base_; }

private:
    FilterPredicate predicate_;
    BaseService base_;
    std::function<void(const auto&)> on_filtered_;

    std::atomic<std::uint64_t> total_messages_{0};
    std::atomic<std::uint64_t> accepted_messages_{0};
    std::atomic<std::uint64_t> filtered_messages_{0};
};

template<typename BaseService, std::size_t MaxRetries = 3>
class Retry : public ServiceBase<Retry<BaseService, MaxRetries>> {
public:
    template<typename... Args>
    explicit Retry(Args&&... args) : base_(std::forward<Args>(args)...) {
        MINFO("Retry service initialized with max retries: {} for base service: {}",
              MaxRetries, typeid(BaseService).name());
    }

    ~Retry() {
        auto stats = get_retry_stats();
        MINFO("Retry service shutdown - Attempts: {}, Successful: {}, Failed: {}, "
              "Completely failed: {}, Success rate: {:.2f}%",
              stats.total_attempts, stats.successful_attempts, stats.failed_attempts,
              stats.completely_failed_messages, stats.success_rate * 100);
    }

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        const auto message_type = typeid(std::decay_t<Message>).name();
        std::size_t attempts = 0;
        bool success = false;
        std::exception_ptr last_exception;

        MDEBUG("Retry starting processing for message type: {} (max retries: {})",
               message_type, MaxRetries);

        while (attempts < MaxRetries && !success) {
            try {
                ++total_attempts_;
                ++attempts;

                MTRACE("Retry attempt {} for message type: {}", attempts, message_type);

                base_.template handle(msg, [&](auto&& processed_msg) {
                    success = true;
                    ++successful_attempts_;
                    MTRACE("Retry successful on attempt {} for message type: {}",
                           attempts, message_type);
                    handler(std::forward<decltype(processed_msg)>(processed_msg));
                });
            } catch (const std::exception& e) {
                last_exception = std::current_exception();
                ++failed_attempts_;

                MWARN("Retry attempt {} failed for message type {}: {}",
                      attempts, message_type, e.what());

                if (attempts >= MaxRetries) {
                    ++completely_failed_messages_;
                    MERROR("Retry exhausted all {} attempts for message type {}: {}",
                           MaxRetries, message_type, e.what());
                    std::rethrow_exception(last_exception);
                }

                // 可选：添加重试延迟
                auto delay = retry_delay_ms_.load();
                if (delay > 0 && attempts < MaxRetries) {
                    MDEBUG("Retry waiting {}ms before next attempt for message type: {}",
                           delay, message_type);
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                }
            } catch (...) {
                last_exception = std::current_exception();
                ++failed_attempts_;

                MWARN("Retry attempt {} failed for message type {}: unknown exception",
                      attempts, message_type);

                if (attempts >= MaxRetries) {
                    ++completely_failed_messages_;
                    MERROR("Retry exhausted all {} attempts for message type {}: unknown exception",
                           MaxRetries, message_type);
                    std::rethrow_exception(last_exception);
                }

                auto delay = retry_delay_ms_.load();
                if (delay > 0 && attempts < MaxRetries) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                }
            }
        }
    }

    void set_retry_delay(std::chrono::milliseconds delay) noexcept {
        auto delay_ms = delay.count();
        retry_delay_ms_ = delay_ms;
        MINFO("Retry delay set to {}ms", delay_ms);
    }

    struct RetryStats {
        std::uint64_t total_attempts;
        std::uint64_t successful_attempts;
        std::uint64_t failed_attempts;
        std::uint64_t completely_failed_messages;
        double success_rate;
    };

    RetryStats get_retry_stats() const noexcept {
        auto total = total_attempts_.load();
        auto successful = successful_attempts_.load();
        auto failed = failed_attempts_.load();
        auto completely_failed = completely_failed_messages_.load();
        double rate = total > 0 ? static_cast<double>(successful) / total : 0.0;

        return {total, successful, failed, completely_failed, rate};
    }

    void reset_retry_stats() noexcept {
        MINFO("Retry resetting statistics");
        total_attempts_ = 0;
        successful_attempts_ = 0;
        failed_attempts_ = 0;
        completely_failed_messages_ = 0;
    }

    void log_retry_stats() const {
        auto stats = get_retry_stats();
        MINFO("Retry stats - Attempts: {}, Successful: {}, Failed: {}, "
              "Completely failed: {}, Success rate: {:.2f}%",
              stats.total_attempts, stats.successful_attempts, stats.failed_attempts,
              stats.completely_failed_messages, stats.success_rate * 100);
    }

    BaseService& base() noexcept { return base_; }
    const BaseService& base() const noexcept { return base_; }

private:
    BaseService base_;
    std::atomic<std::uint64_t> retry_delay_ms_{0};

    std::atomic<std::uint64_t> total_attempts_{0};
    std::atomic<std::uint64_t> successful_attempts_{0};
    std::atomic<std::uint64_t> failed_attempts_{0};
    std::atomic<std::uint64_t> completely_failed_messages_{0};
};

template<typename BaseService>
auto with_statistics(BaseService&& base) {
    return Statistics<std::decay_t<BaseService>>(std::forward<BaseService>(base));
}

template<typename FilterPredicate, typename BaseService>
auto with_filter(FilterPredicate&& pred, BaseService&& base) {
    return Filter<std::decay_t<BaseService>, std::decay_t<FilterPredicate>>(
        std::forward<FilterPredicate>(pred),
        std::forward<BaseService>(base)
    );
}

template<std::size_t MaxRetries = 3, typename BaseService>
auto with_retry(BaseService&& base) {
    return Retry<std::decay_t<BaseService>, MaxRetries>(std::forward<BaseService>(base));
}

template<typename BaseService>
auto with_full_monitoring(BaseService&& base) {
    return with_statistics(
        with_retry<3>(
            std::forward<BaseService>(base)
        )
    );
}

} // namespace comm::service

namespace comm::traits {

template<>
struct is_realtime_capable<comm::service::None> : std::true_type {};

template<>
struct is_realtime_capable<comm::service::SimpleRouter> : std::true_type {};

template<typename T>
struct is_realtime_capable<comm::service::Async<T>> : std::false_type {};

template<typename T>
struct is_realtime_capable<comm::service::Statistics<T>> : is_realtime_capable<T> {};

template<typename T, typename P>
struct is_realtime_capable<comm::service::Filter<T, P>> : is_realtime_capable<T> {};

template<typename T, std::size_t R>
struct is_realtime_capable<comm::service::Retry<T, R>> : is_realtime_capable<T> {};

template<typename T>
struct memory_model<comm::service::None> {
    static constexpr bool is_static = true;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_pool_based = false;
};

template<typename T>
struct memory_model<comm::service::Async<T>> {
    static constexpr bool is_static = false;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_pool_based = true;
};

template<typename T>
struct memory_model<comm::service::Statistics<T>> : memory_model<T> {};

template<typename T, typename P>
struct memory_model<comm::service::Filter<T, P>> : memory_model<T> {};

template<typename T, std::size_t R>
struct memory_model<comm::service::Retry<T, R>> : memory_model<T> {};

} // namespace comm::traits

#endif // COMM_SERVICE_NONE_HPP