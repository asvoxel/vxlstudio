// TransportServer implementation -- JSON-over-TCP server.
// SPDX-License-Identifier: MIT

#include "vxl/transport.h"
#include "protocol.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace vxl {

struct TransportServer::Impl {
    std::string address_;
    int port_ = 0;               // actual bound port
    std::atomic<bool> running_{false};

    socket_t listen_sock_ = VXL_INVALID_SOCKET;

    // Registered request handlers
    std::mutex handlers_mu_;
    std::unordered_map<std::string, RequestHandler> handlers_;

    // Connected client sockets (for broadcast and cleanup)
    std::mutex clients_mu_;
    std::vector<socket_t> client_sockets_;

    // Client handler threads (detached threads that we track for join on stop)
    std::mutex threads_mu_;
    std::vector<std::thread> client_threads_;

    // Accept thread
    std::thread accept_thread_;

    // Pipe for waking up select() on stop (POSIX only)
#ifndef _WIN32
    int wake_pipe_[2] = {-1, -1};
#endif

    void accept_loop();
    void client_loop(socket_t client_sock);
    void add_client_socket(socket_t sock);
    void remove_client_socket(socket_t sock);
};

TransportServer::TransportServer() : impl_(std::make_unique<Impl>()) {}

TransportServer::~TransportServer() {
    stop();
}

Result<void> TransportServer::start(const std::string& address, int port) {
    if (impl_->running_) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER, "server already running");
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    impl_->listen_sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->listen_sock_ == VXL_INVALID_SOCKET) {
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED, "socket() failed");
    }

    proto::set_reuse_addr(impl_->listen_sock_);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (address.empty() || address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, address.c_str(), &addr.sin_addr);
    }

    if (::bind(impl_->listen_sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == VXL_SOCKET_ERROR) {
        vxl_close_socket(impl_->listen_sock_);
        impl_->listen_sock_ = VXL_INVALID_SOCKET;
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED, "bind() failed");
    }

    if (::listen(impl_->listen_sock_, 16) == VXL_SOCKET_ERROR) {
        vxl_close_socket(impl_->listen_sock_);
        impl_->listen_sock_ = VXL_INVALID_SOCKET;
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED, "listen() failed");
    }

    // Retrieve actual port (useful when port==0 for ephemeral port)
    struct sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    getsockname(impl_->listen_sock_, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len);
    impl_->port_ = ntohs(bound_addr.sin_port);
    impl_->address_ = address.empty() ? "0.0.0.0" : address;

#ifndef _WIN32
    if (::pipe(impl_->wake_pipe_) != 0) {
        vxl_close_socket(impl_->listen_sock_);
        impl_->listen_sock_ = VXL_INVALID_SOCKET;
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, "pipe() failed");
    }
#endif

    impl_->running_ = true;
    impl_->accept_thread_ = std::thread([this]() { impl_->accept_loop(); });

    return Result<void>::success();
}

void TransportServer::stop() {
    if (!impl_->running_) return;
    impl_->running_ = false;

#ifndef _WIN32
    // Wake up the accept select()
    if (impl_->wake_pipe_[1] >= 0) {
        char c = 'x';
        (void)::write(impl_->wake_pipe_[1], &c, 1);
    }
#endif

    // Close listen socket to unblock accept
    if (impl_->listen_sock_ != VXL_INVALID_SOCKET) {
        vxl_close_socket(impl_->listen_sock_);
        impl_->listen_sock_ = VXL_INVALID_SOCKET;
    }

    if (impl_->accept_thread_.joinable()) {
        impl_->accept_thread_.join();
    }

    // Close all client sockets to unblock their reader loops and handlers
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mu_);
        for (auto sock : impl_->client_sockets_) {
            // Shutdown first to unblock any recv/send in handler
            ::shutdown(sock,
#ifdef _WIN32
                       SD_BOTH
#else
                       SHUT_RDWR
#endif
            );
            vxl_close_socket(sock);
        }
        impl_->client_sockets_.clear();
    }

    // Join all client handler threads
    {
        std::lock_guard<std::mutex> lock(impl_->threads_mu_);
        for (auto& t : impl_->client_threads_) {
            if (t.joinable()) t.join();
        }
        impl_->client_threads_.clear();
    }

#ifndef _WIN32
    if (impl_->wake_pipe_[0] >= 0) { ::close(impl_->wake_pipe_[0]); impl_->wake_pipe_[0] = -1; }
    if (impl_->wake_pipe_[1] >= 0) { ::close(impl_->wake_pipe_[1]); impl_->wake_pipe_[1] = -1; }
#endif
}

bool TransportServer::is_running() const {
    return impl_->running_;
}

void TransportServer::on_request(const std::string& method, RequestHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->handlers_mu_);
    impl_->handlers_[method] = std::move(handler);
}

Result<void> TransportServer::broadcast_event(const TransportMessage& event) {
    std::lock_guard<std::mutex> lock(impl_->clients_mu_);
    for (auto sock : impl_->client_sockets_) {
        proto::send_message(sock, event); // best-effort
    }
    return Result<void>::success();
}

int TransportServer::connected_clients() const {
    std::lock_guard<std::mutex> lock(impl_->clients_mu_);
    return static_cast<int>(impl_->client_sockets_.size());
}

std::string TransportServer::address() const {
    return impl_->address_;
}

int TransportServer::port() const {
    return impl_->port_;
}

// ---- Impl methods ----------------------------------------------------------

void TransportServer::Impl::add_client_socket(socket_t sock) {
    std::lock_guard<std::mutex> lock(clients_mu_);
    client_sockets_.push_back(sock);
}

void TransportServer::Impl::remove_client_socket(socket_t sock) {
    std::lock_guard<std::mutex> lock(clients_mu_);
    client_sockets_.erase(
        std::remove(client_sockets_.begin(), client_sockets_.end(), sock),
        client_sockets_.end());
}

void TransportServer::Impl::accept_loop() {
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_sock_, &fds);

        int max_fd = static_cast<int>(listen_sock_);

#ifndef _WIN32
        if (wake_pipe_[0] >= 0) {
            FD_SET(wake_pipe_[0], &fds);
            if (wake_pipe_[0] > max_fd) max_fd = wake_pipe_[0];
        }
#endif

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = ::select(max_fd + 1, &fds, nullptr, nullptr, &tv);
        if (!running_) break;
        if (sel <= 0) continue;

#ifndef _WIN32
        if (wake_pipe_[0] >= 0 && FD_ISSET(wake_pipe_[0], &fds)) {
            break;
        }
#endif

        if (!FD_ISSET(listen_sock_, &fds)) continue;

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        socket_t client_sock = ::accept(listen_sock_,
                                        reinterpret_cast<struct sockaddr*>(&client_addr),
                                        &client_len);
        if (client_sock == VXL_INVALID_SOCKET) continue;

        proto::set_tcp_nodelay(client_sock);
        add_client_socket(client_sock);

        std::lock_guard<std::mutex> lock(threads_mu_);
        client_threads_.emplace_back([this, client_sock]() { client_loop(client_sock); });
    }
}

void TransportServer::Impl::client_loop(socket_t client_sock) {
    while (running_) {
        // Use select to wait for data with timeout, so we can check running_ periodically
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(client_sock, &fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms

        int sel = ::select(static_cast<int>(client_sock) + 1, &fds, nullptr, nullptr, &tv);
        if (!running_) break;
        if (sel < 0) break; // error (e.g., socket closed during shutdown)
        if (sel == 0) continue; // timeout

        auto result = proto::recv_message(client_sock, 5000);
        if (!result.ok()) {
            break; // disconnect or error
        }

        const auto& msg = result.value;

        if (msg.type == "request") {
            TransportMessage response;
            response.type = "response";
            response.method = msg.method;
            response.id = msg.id;
            response.payload = "{}";

            // Look up handler under lock, then invoke outside the lock
            RequestHandler handler;
            {
                std::lock_guard<std::mutex> lock(handlers_mu_);
                auto it = handlers_.find(msg.method);
                if (it != handlers_.end()) {
                    handler = it->second;
                }
            }

            if (handler) {
                response = handler(msg);
                response.type = "response";
                response.id = msg.id;
            } else {
                response.payload = "{\"error\":\"unknown method\"}";
            }

            if (!running_) break;
            if (!proto::send_message(client_sock, response)) break;
        }
    }

    remove_client_socket(client_sock);
    // Note: we don't close the socket here -- stop() closes all sockets.
    // But if we're exiting due to peer disconnect (not shutdown), close it.
    if (running_) {
        vxl_close_socket(client_sock);
    }
}

} // namespace vxl
