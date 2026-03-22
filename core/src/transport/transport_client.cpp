// TransportClient implementation -- JSON-over-TCP client.
// SPDX-License-Identifier: MIT

#include "vxl/transport.h"
#include "protocol.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace vxl {

struct TransportClient::Impl {
    socket_t sock_ = VXL_INVALID_SOCKET;
    std::atomic<bool> connected_{false};
    std::atomic<int64_t> next_id_{1};

    // Background reader thread
    std::thread reader_thread_;

    // Self-pipe for clean shutdown
#ifndef _WIN32
    int wake_pipe_[2] = {-1, -1};
#endif

    // Event callback
    std::mutex event_mu_;
    EventCallback event_callback_;

    // Pending responses: id -> (message, ready flag)
    struct PendingResponse {
        TransportMessage msg;
        bool ready = false;
    };
    std::mutex pending_mu_;
    std::condition_variable pending_cv_;
    std::unordered_map<int64_t, PendingResponse> pending_;

    void reader_loop();
};

TransportClient::TransportClient() : impl_(std::make_unique<Impl>()) {}

TransportClient::~TransportClient() {
    disconnect();
}

Result<void> TransportClient::connect(const std::string& address, int port) {
    if (impl_->connected_) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER, "already connected");
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    impl_->sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->sock_ == VXL_INVALID_SOCKET) {
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED, "socket() failed");
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port));

    std::string resolved = address;
    if (resolved.empty() || resolved == "localhost") {
        resolved = "127.0.0.1";
    }
    if (inet_pton(AF_INET, resolved.c_str(), &server_addr.sin_addr) <= 0) {
        vxl_close_socket(impl_->sock_);
        impl_->sock_ = VXL_INVALID_SOCKET;
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED, "invalid address");
    }

    if (::connect(impl_->sock_, reinterpret_cast<struct sockaddr*>(&server_addr),
                  sizeof(server_addr)) == VXL_SOCKET_ERROR) {
        vxl_close_socket(impl_->sock_);
        impl_->sock_ = VXL_INVALID_SOCKET;
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED, "connect() failed");
    }

    proto::set_tcp_nodelay(impl_->sock_);

#ifndef _WIN32
    if (::pipe(impl_->wake_pipe_) != 0) {
        vxl_close_socket(impl_->sock_);
        impl_->sock_ = VXL_INVALID_SOCKET;
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, "pipe() failed");
    }
#endif

    impl_->connected_ = true;

    // Start background reader
    impl_->reader_thread_ = std::thread([this]() { impl_->reader_loop(); });

    return Result<void>::success();
}

void TransportClient::disconnect() {
    if (!impl_->connected_) return;
    impl_->connected_ = false;

#ifndef _WIN32
    // Signal reader thread to wake up via pipe
    if (impl_->wake_pipe_[1] >= 0) {
        char c = 'x';
        (void)::write(impl_->wake_pipe_[1], &c, 1);
    }
#endif

    // Wake up any pending requests
    {
        std::lock_guard<std::mutex> lock(impl_->pending_mu_);
        for (auto& [id, pr] : impl_->pending_) {
            pr.ready = true;
        }
        impl_->pending_cv_.notify_all();
    }

    if (impl_->reader_thread_.joinable()) {
        impl_->reader_thread_.join();
    }

    // Close socket AFTER reader thread has exited
    if (impl_->sock_ != VXL_INVALID_SOCKET) {
        vxl_close_socket(impl_->sock_);
        impl_->sock_ = VXL_INVALID_SOCKET;
    }

#ifndef _WIN32
    if (impl_->wake_pipe_[0] >= 0) { ::close(impl_->wake_pipe_[0]); impl_->wake_pipe_[0] = -1; }
    if (impl_->wake_pipe_[1] >= 0) { ::close(impl_->wake_pipe_[1]); impl_->wake_pipe_[1] = -1; }
#endif
}

bool TransportClient::is_connected() const {
    return impl_->connected_;
}

Result<TransportMessage> TransportClient::request(const std::string& method,
                                                   const std::string& payload_json,
                                                   int timeout_ms) {
    if (!impl_->connected_) {
        return Result<TransportMessage>::failure(ErrorCode::IO_CONNECTION_FAILED, "not connected");
    }

    int64_t id = impl_->next_id_++;

    // Register pending response slot
    {
        std::lock_guard<std::mutex> lock(impl_->pending_mu_);
        impl_->pending_[id] = {};
    }

    TransportMessage req;
    req.type = "request";
    req.method = method;
    req.id = id;
    req.payload = payload_json.empty() ? "{}" : payload_json;

    if (!proto::send_message(impl_->sock_, req)) {
        std::lock_guard<std::mutex> lock(impl_->pending_mu_);
        impl_->pending_.erase(id);
        return Result<TransportMessage>::failure(ErrorCode::IO_WRITE_FAILED, "send failed");
    }

    // Wait for response with timeout
    {
        std::unique_lock<std::mutex> lock(impl_->pending_mu_);
        bool ok = impl_->pending_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [&]() { return impl_->pending_[id].ready; }
        );

        if (!ok) {
            impl_->pending_.erase(id);
            return Result<TransportMessage>::failure(ErrorCode::DEVICE_TIMEOUT, "request timed out");
        }

        auto response = std::move(impl_->pending_[id].msg);
        impl_->pending_.erase(id);

        if (!impl_->connected_ && response.type.empty()) {
            return Result<TransportMessage>::failure(ErrorCode::IO_CONNECTION_FAILED, "disconnected");
        }

        return Result<TransportMessage>::success(std::move(response));
    }
}

void TransportClient::on_event(EventCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->event_mu_);
    impl_->event_callback_ = std::move(callback);
}

// ---- Impl methods ----------------------------------------------------------

void TransportClient::Impl::reader_loop() {
    while (connected_) {
        // Use select() to wait on both the socket and the wake pipe
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        int max_fd = static_cast<int>(sock_);

#ifndef _WIN32
        if (wake_pipe_[0] >= 0) {
            FD_SET(wake_pipe_[0], &fds);
            if (wake_pipe_[0] > max_fd) max_fd = wake_pipe_[0];
        }
#endif

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms

        int sel = ::select(max_fd + 1, &fds, nullptr, nullptr, &tv);
        if (!connected_) break;
        if (sel <= 0) continue; // timeout or error

#ifndef _WIN32
        // Check wake pipe
        if (wake_pipe_[0] >= 0 && FD_ISSET(wake_pipe_[0], &fds)) {
            break; // shutdown requested
        }
#endif

        if (!FD_ISSET(sock_, &fds)) continue;

        // Data available on socket -- read the message
        auto result = proto::recv_message(sock_, 5000);
        if (!result.ok()) {
            if (!connected_) break;
            // Connection lost
            connected_ = false;
            // Wake up pending requests
            {
                std::lock_guard<std::mutex> lock(pending_mu_);
                for (auto& [id, pr] : pending_) {
                    pr.ready = true;
                }
                pending_cv_.notify_all();
            }
            break;
        }

        const auto& msg = result.value;

        if (msg.type == "response") {
            std::lock_guard<std::mutex> lock(pending_mu_);
            auto it = pending_.find(msg.id);
            if (it != pending_.end()) {
                it->second.msg = msg;
                it->second.ready = true;
                pending_cv_.notify_all();
            }
        } else if (msg.type == "event") {
            std::lock_guard<std::mutex> lock(event_mu_);
            if (event_callback_) {
                event_callback_(msg);
            }
        }
    }
}

} // namespace vxl
