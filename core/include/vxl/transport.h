#pragma once
#include "vxl/export.h"
#include "vxl/error.h"
#include "vxl/types.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace vxl {

// Transport message (JSON-over-TCP)
struct VXL_EXPORT TransportMessage {
    std::string type;       // "request", "response", "event"
    std::string method;     // e.g., "inspect", "get_status", "set_param"
    std::string payload;    // JSON string
    int64_t id = 0;         // request/response correlation
};

// Request handler callback
using RequestHandler = std::function<TransportMessage(const TransportMessage& request)>;

// Event callback (for subscribed events)
using EventCallback = std::function<void(const TransportMessage& event)>;

// Server: listens for connections, dispatches requests to handlers
class VXL_EXPORT TransportServer {
public:
    TransportServer();
    ~TransportServer();

    TransportServer(const TransportServer&) = delete;
    TransportServer& operator=(const TransportServer&) = delete;

    Result<void> start(const std::string& address, int port);
    void stop();
    bool is_running() const;

    // Register method handlers
    void on_request(const std::string& method, RequestHandler handler);

    // Broadcast event to all connected clients
    Result<void> broadcast_event(const TransportMessage& event);

    // Info
    int connected_clients() const;
    std::string address() const;
    int port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Client: connects to server, sends requests, receives events
class VXL_EXPORT TransportClient {
public:
    TransportClient();
    ~TransportClient();

    TransportClient(const TransportClient&) = delete;
    TransportClient& operator=(const TransportClient&) = delete;

    Result<void> connect(const std::string& address, int port);
    void disconnect();
    bool is_connected() const;

    // Send request and wait for response
    Result<TransportMessage> request(const std::string& method, const std::string& payload_json,
                                      int timeout_ms = 5000);

    // Subscribe to server events
    void on_event(EventCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
