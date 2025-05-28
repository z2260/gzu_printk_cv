#pragma once

#ifndef COMM_CORE_PIPELINE_HPP
#define COMM_CORE_PIPELINE_HPP

#include "traits.hpp"
#include "endpoint.hpp"
#include "frame.hpp"
#include <type_traits>
#include <functional>
#include <utility>
#include <atomic>
#include <chrono>
#include <thread>

namespace comm::core {

template<typename LinkPolicy, typename TransportPolicy,
         typename MessagePolicy, typename ServicePolicy>
class Pipeline;

/**************************************************
*
***************************************************/
template<typename Derived>
class PipelineBase {
public:
    Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    const Derived& derived() const noexcept {
        return static_cast<const Derived&>(*this);
    }

protected:
    PipelineBase() = default;
    ~PipelineBase() = default;

    PipelineBase(const PipelineBase&) = delete;
    PipelineBase& operator=(const PipelineBase&) = delete;
    PipelineBase(PipelineBase&&) = delete;
    PipelineBase& operator=(PipelineBase&&) = delete;
};

/**************************************************
*
***************************************************/
template<typename LinkPolicy, typename TransportPolicy,
         typename MessagePolicy, typename ServicePolicy>
class Pipeline : public PipelineBase<Pipeline<LinkPolicy, TransportPolicy, MessagePolicy, ServicePolicy>>,
                 private LinkPolicy,
                 private TransportPolicy,
                 private MessagePolicy,
                 private ServicePolicy {
public:
    using link_policy_t = LinkPolicy;
    using transport_policy_t = TransportPolicy;
    using message_policy_t = MessagePolicy;
    using service_policy_t = ServicePolicy;
    using self_t = Pipeline<LinkPolicy, TransportPolicy, MessagePolicy, ServicePolicy>;

    static_assert(traits::is_link_policy_v<LinkPolicy>,
                  "LinkPolicy must satisfy link policy requirements");
    static_assert(traits::is_transport_policy_v<TransportPolicy>,
                  "TransportPolicy must satisfy transport policy requirements");
    static_assert(traits::is_message_policy_v<MessagePolicy>,
                  "MessagePolicy must satisfy message policy requirements");
    static_assert(traits::is_service_policy_v<ServicePolicy>,
                  "ServicePolicy must satisfy service policy requirements");

    Pipeline() : running_(false) {}

    Pipeline(const LinkPolicy& link, const TransportPolicy& transport,
             const MessagePolicy& message, const ServicePolicy& service)
        : LinkPolicy(link), TransportPolicy(transport),
          MessagePolicy(message), ServicePolicy(service), running_(false) {}

    Pipeline(Pipeline&&) = default;
    Pipeline& operator=(Pipeline&&) = default;

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    LinkPolicy& link() noexcept { return static_cast<LinkPolicy&>(*this); }
    const LinkPolicy& link() const noexcept { return static_cast<const LinkPolicy&>(*this); }

    TransportPolicy& transport() noexcept { return static_cast<TransportPolicy&>(*this); }
    const TransportPolicy& transport() const noexcept { return static_cast<const TransportPolicy&>(*this); }

    MessagePolicy& message() noexcept { return static_cast<MessagePolicy&>(*this); }
    const MessagePolicy& message() const noexcept { return static_cast<const MessagePolicy&>(*this); }

    ServicePolicy& service() noexcept { return static_cast<ServicePolicy&>(*this); }
    const ServicePolicy& service() const noexcept { return static_cast<const ServicePolicy&>(*this); }

    template<typename T>
    bool send(const EndpointID& dst, const T& obj) {
        auto encoded = message().template encode<T>(obj);
        if (!encoded) {
            return false;
        }

        auto wrapped = transport().wrap(traits::buffer_view<const std::uint8_t>(
            encoded->data(), encoded->size()));
        if (!wrapped) {
            return false;
        }

        return link().write(dst.node_id, traits::buffer_view<const std::uint8_t>(
            wrapped->data(), wrapped->size()));
    }

    bool send_buffer(const EndpointID& dst, traits::buffer_view<const std::uint8_t> data) {
        auto wrapped = transport().wrap(data);
        if (!wrapped) {
            return false;
        }

        return link().write(dst.node_id, traits::buffer_view<const std::uint8_t>(
            wrapped->data(), wrapped->size()));
    }

    template<typename Handler>
    void loop(Handler&& handler) {
        static_assert(traits::is_callable_r_v<void, Handler>,
                      "Handler must be callable");

        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            if (!process_one(std::forward<Handler>(handler))) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }

    template<typename Handler>
    void loop_for(Handler&& handler, std::chrono::milliseconds timeout) {
        static_assert(traits::is_callable_r_v<void, Handler>,
                      "Handler must be callable");

        running_.store(true, std::memory_order_release);
        auto start_time = std::chrono::steady_clock::now();

        while (running_.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() - start_time >= timeout) {
                break;
            }

            if (!process_one(std::forward<Handler>(handler))) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }

    void stop() noexcept {
        running_.store(false, std::memory_order_release);
    }

    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    /**************************************************
    *
    ***************************************************/
    template<typename Handler>
    bool process_one(Handler&& handler) {
        static_assert(traits::is_callable_r_v<void, Handler>,
                      "Handler must be callable");

        auto packet = link().read();
        if (!packet) {
            return false;
        }

        auto unwrapped = transport().unwrap(traits::buffer_view<const std::uint8_t>(
            packet->data(), packet->size()));
        if (!unwrapped) {
            return false;
        }

        message().dispatch(traits::buffer_view<const std::uint8_t>(
            unwrapped->data(), unwrapped->size()),
            [&](auto&& msg) {
                service().template handle(std::forward<decltype(msg)>(msg),
                                        std::forward<Handler>(handler));
            });

        return true;
    }

    /**************************************************
    *
    ***************************************************/
    std::size_t mtu() const {
        return link().mtu();
    }

    bool is_connected() const {
        if constexpr (traits::has_is_connected_v<LinkPolicy>) {
            return link().is_connected();
        } else {
            return true;
        }
    }

    void close() {
        stop();
        if constexpr (traits::has_close_v<LinkPolicy>) {
            link().close();
        }
    }

    auto get_stats() const {
        if constexpr (traits::has_get_stats_v<LinkPolicy>) {
            return link().get_stats();
        } else if constexpr (traits::has_get_stats_v<TransportPolicy>) {
            return transport().get_stats();
        } else {
            struct EmptyStats {
                std::size_t bytes_sent = 0;
                std::size_t bytes_received = 0;
                std::size_t packets_sent = 0;
                std::size_t packets_received = 0;
            };
            return EmptyStats{};
        }
    }

    template<typename Iterator>
    std::size_t send_batch(const EndpointID& dst, Iterator begin, Iterator end) {
        std::size_t sent_count = 0;
        for (auto it = begin; it != end; ++it) {
            if (send(dst, *it)) {
                ++sent_count;
            } else {
                break;
            }
        }
        return sent_count;
    }

    template<typename T, typename Callback>
    bool send_async(const EndpointID& dst, const T& obj, Callback&& callback) {
        bool result = send(dst, obj);
        callback(result);
        return result;
    }

private:
    std::atomic<bool> running_;
};

/**************************************************
*
***************************************************/
template<typename LinkPolicy>
class PipelineBuilder {
public:
    explicit PipelineBuilder(LinkPolicy&& link)
        : link_(std::forward<LinkPolicy>(link)) {}

    template<typename TransportPolicy>
    auto transport(TransportPolicy&& tp) && {
        return PipelineBuilder2<LinkPolicy, TransportPolicy>(
            std::move(link_), std::forward<TransportPolicy>(tp));
    }

private:
    LinkPolicy link_;

    template<typename L, typename T>
    friend class PipelineBuilder2;
};

template<typename LinkPolicy, typename TransportPolicy>
class PipelineBuilder2 {
public:
    PipelineBuilder2(LinkPolicy&& link, TransportPolicy&& transport)
        : link_(std::forward<LinkPolicy>(link))
        , transport_(std::forward<TransportPolicy>(transport)) {}

    template<typename MessagePolicy>
    auto message(MessagePolicy&& mp) && {
        return PipelineBuilder3<LinkPolicy, TransportPolicy, MessagePolicy>(
            std::move(link_), std::move(transport_), std::forward<MessagePolicy>(mp));
    }

private:
    LinkPolicy link_;
    TransportPolicy transport_;

    template<typename L, typename T, typename M>
    friend class PipelineBuilder3;
};

template<typename LinkPolicy, typename TransportPolicy, typename MessagePolicy>
class PipelineBuilder3 {
public:
    PipelineBuilder3(LinkPolicy&& link, TransportPolicy&& transport, MessagePolicy&& message)
        : link_(std::forward<LinkPolicy>(link))
        , transport_(std::forward<TransportPolicy>(transport))
        , message_(std::forward<MessagePolicy>(message)) {}

    template<typename ServicePolicy>
    auto service(ServicePolicy&& sp) && {
        return Pipeline<LinkPolicy, TransportPolicy, MessagePolicy, ServicePolicy>(
            std::move(link_), std::move(transport_),
            std::move(message_), std::forward<ServicePolicy>(sp));
    }

private:
    LinkPolicy link_;
    TransportPolicy transport_;
    MessagePolicy message_;
};

/**************************************************
*
***************************************************/
template<typename LinkPolicy>
auto make_pipeline(LinkPolicy&& link) {
    return PipelineBuilder<LinkPolicy>(std::forward<LinkPolicy>(link));
}

/**************************************************
*
***************************************************/
template<typename LinkPolicy, typename TransportPolicy,
         typename MessagePolicy, typename ServicePolicy>
auto create_pipeline(LinkPolicy&& link, TransportPolicy&& transport,
                    MessagePolicy&& message, ServicePolicy&& service) {
    static_assert(traits::is_valid_pipeline_combination_v<
        std::decay_t<LinkPolicy>, std::decay_t<TransportPolicy>,
        std::decay_t<MessagePolicy>, std::decay_t<ServicePolicy>>,
        "Invalid pipeline policy combination");

    return Pipeline<std::decay_t<LinkPolicy>, std::decay_t<TransportPolicy>,
                   std::decay_t<MessagePolicy>, std::decay_t<ServicePolicy>>(
        std::forward<LinkPolicy>(link), std::forward<TransportPolicy>(transport),
        std::forward<MessagePolicy>(message), std::forward<ServicePolicy>(service));
}

/**************************************************
*
***************************************************/
namespace pipelines {
    template<std::size_t BufferSize = 1024>
    using MinimalMCU = Pipeline<
        /* LinkPolicy */ void,
        /* TransportPolicy */ void,
        /* MessagePolicy */ void,
        /* ServicePolicy */ void
    >;

    template<std::size_t BufferSize = 4096>
    using HighPerformance = Pipeline<
        /* LinkPolicy */ void,
        /* TransportPolicy */ void,
        /* MessagePolicy */ void,
        /* ServicePolicy */ void
    >;

    template<std::size_t BufferSize = 2048>
    using Realtime = Pipeline<
        /* LinkPolicy */ void,
        /* TransportPolicy */ void,
        /* MessagePolicy */ void,
        /* ServicePolicy */ void
    >;
} // namespace pipelines

} // namespace comm::core

#endif // COMM_CORE_PIPELINE_HPP