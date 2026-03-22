#include "vxl/message_bus.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace vxl {

// ===========================================================================
// MessageBus::Impl
// ===========================================================================

struct MessageBus::Impl {
    std::mutex                                                       mutex;
    std::unordered_map<std::string, std::vector<MessageBus::Subscriber>> subscribers;
    std::atomic<uint64_t>                                            next_id{1};
};

MessageBus::MessageBus() = default;
MessageBus::~MessageBus() = default;

MessageBus::Impl& MessageBus::get_impl() {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    return *impl_;
}

void MessageBus::publish(const std::shared_ptr<Message>& msg) {
    if (!msg) return;

    std::vector<Handler> handlers;
    {
        auto& impl = get_impl();
        std::lock_guard<std::mutex> lock(impl.mutex);
        auto it = impl.subscribers.find(msg->topic);
        if (it != impl.subscribers.end()) {
            handlers.reserve(it->second.size());
            for (const auto& sub : it->second) {
                handlers.push_back(sub.handler);
            }
        }
    }

    // Dispatch synchronously outside the lock
    for (const auto& handler : handlers) {
        handler(msg);
    }
}

uint64_t MessageBus::subscribe(const std::string& topic, Handler handler) {
    auto& impl = get_impl();
    uint64_t id = impl.next_id.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        impl.subscribers[topic].push_back(Subscriber{id, std::move(handler)});
    }
    return id;
}

void MessageBus::unsubscribe(uint64_t id) {
    auto& impl = get_impl();
    std::lock_guard<std::mutex> lock(impl.mutex);
    for (auto& [topic, subs] : impl.subscribers) {
        for (auto it = subs.begin(); it != subs.end(); ++it) {
            if (it->id == id) {
                subs.erase(it);
                return;
            }
        }
    }
}

// ===========================================================================
// Global accessor
// ===========================================================================

MessageBus& message_bus() {
    static MessageBus instance;
    return instance;
}

} // namespace vxl
