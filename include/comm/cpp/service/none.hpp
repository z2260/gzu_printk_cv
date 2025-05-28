#pragma once

#ifndef COMM_SERVICE_NONE_HPP
#define COMM_SERVICE_NONE_HPP

#include "../core/traits.hpp"
#include <functional>
#include <type_traits>
#include <exception>

namespace comm::service {

template<typename Derived>
class ServiceBase {
public:
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }
    const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

    template<typename Message, typename Handler>
    void handle(Message&& msg, Handler&& handler) {
        derived().template handle_impl(std::forward<Message>(msg), std::forward<Handler>(handler));
    }

protected:
    ServiceBase() = default;
    ~ServiceBase() = default;
};

class None : public ServiceBase<None> {
public:
    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        handler(std::forward<Message>(msg));
    }
};

class SimpleRouter : public ServiceBase<SimpleRouter> {
public:
    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        handler(std::forward<Message>(msg));
    }

    template<typename MessageType, typename SpecificHandler>
    void register_handler(SpecificHandler&& handler) {

    }
};

template<std::size_t QueueSize = 256>
class Async : public ServiceBase<Async<QueueSize>> {
public:
    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        handler(std::forward<Message>(msg));
    }

    void process_queue() {
        // TODO: 实现队列处理逻辑
    }

    std::size_t queue_size() const noexcept {
        // TODO: 返回实际队列大小
        return 0; // 暂时返回0，等待实现
    }

    bool is_queue_full() const noexcept { return false; }
};

template<typename BaseService>
class Statistics : public ServiceBase<Statistics<BaseService>> {
public:
    template<typename... Args>
    explicit Statistics(Args&&... args) : base_(std::forward<Args>(args)...) {}

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        ++total_messages_;

        try {
            base_.template handle(std::forward<Message>(msg), [&](auto&& processed_msg) {
                ++processed_messages_;
                handler(std::forward<decltype(processed_msg)>(processed_msg));
            });
        } catch (...) {
            ++error_messages_;
            throw; // 重新抛出异常
        }
    }

    struct Stats {
        std::uint64_t total_messages;
        std::uint64_t processed_messages;
        std::uint64_t error_messages;
    };

    Stats get_stats() const noexcept {
        return {total_messages_, processed_messages_, error_messages_};
    }

    void reset_stats() noexcept {
        total_messages_ = 0;
        processed_messages_ = 0;
        error_messages_ = 0;
    }

    BaseService& base() noexcept { return base_; }
    const BaseService& base() const noexcept { return base_; }

private:
    BaseService base_;
    std::uint64_t total_messages_ = 0;
    std::uint64_t processed_messages_ = 0;
    std::uint64_t error_messages_ = 0;
};

template<typename BaseService, typename FilterPredicate>
class Filter : public ServiceBase<Filter<BaseService, FilterPredicate>> {
public:
    template<typename... Args>
    explicit Filter(FilterPredicate pred, Args&&... args)
        : predicate_(std::move(pred)), base_(std::forward<Args>(args)...) {}

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        if (predicate_(msg)) {
            base_.template handle(std::forward<Message>(msg), std::forward<Handler>(handler));
        }
    }

    BaseService& base() noexcept { return base_; }
    const BaseService& base() const noexcept { return base_; }

private:
    FilterPredicate predicate_;
    BaseService base_;
};

template<typename BaseService, std::size_t MaxRetries = 3>
class Retry : public ServiceBase<Retry<BaseService, MaxRetries>> {
public:
    template<typename... Args>
    explicit Retry(Args&&... args) : base_(std::forward<Args>(args)...) {}

    template<typename Message, typename Handler>
    void handle_impl(Message&& msg, Handler&& handler) {
        std::size_t attempts = 0;
        bool success = false;
        std::exception_ptr last_exception;

        while (attempts < MaxRetries && !success) {
            try {
                base_.template handle(msg, [&](auto&& processed_msg) {
                    success = true;
                    handler(std::forward<decltype(processed_msg)>(processed_msg));
                });
            } catch (...) {
                last_exception = std::current_exception();
                ++attempts;
                if (attempts >= MaxRetries) {
                    std::rethrow_exception(last_exception);
                }
            }
        }
    }

    BaseService& base() noexcept { return base_; }
    const BaseService& base() const noexcept { return base_; }

private:
    BaseService base_;
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

} // namespace comm::service

namespace comm::traits {

template<>
struct is_realtime_capable<comm::service::None> : std::true_type {};

template<typename T>
struct is_realtime_capable<comm::service::Async<T>> : std::false_type {};

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

} // namespace comm::traits

#endif // COMM_SERVICE_NONE_HPP