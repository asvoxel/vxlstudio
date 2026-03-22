// Transport module tests -- JSON-over-TCP server/client.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "vxl/transport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

static constexpr int TEST_PORT = 19876;

// Helper: wait until a condition becomes true (with timeout).
template <typename Pred>
bool wait_until(Pred pred, int timeout_ms = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

// ===========================================================================
// Server lifecycle
// ===========================================================================

TEST(TransportServer, StartAndStop) {
    vxl::TransportServer server;
    EXPECT_FALSE(server.is_running());

    auto r = server.start("127.0.0.1", 0);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_TRUE(server.is_running());
    EXPECT_GT(server.port(), 0);
    EXPECT_EQ(server.connected_clients(), 0);

    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST(TransportServer, DoubleStartFails) {
    vxl::TransportServer server;
    auto r1 = server.start("127.0.0.1", 0);
    ASSERT_TRUE(r1.ok());

    auto r2 = server.start("127.0.0.1", 0);
    EXPECT_FALSE(r2.ok());

    server.stop();
}

// ===========================================================================
// Client lifecycle
// ===========================================================================

TEST(TransportClient, ConnectAndDisconnect) {
    vxl::TransportServer server;
    auto r = server.start("127.0.0.1", 0);
    ASSERT_TRUE(r.ok());

    vxl::TransportClient client;
    EXPECT_FALSE(client.is_connected());

    auto cr = client.connect("127.0.0.1", server.port());
    ASSERT_TRUE(cr.ok()) << cr.message;
    EXPECT_TRUE(client.is_connected());

    // Give server time to accept
    EXPECT_TRUE(wait_until([&]() { return server.connected_clients() == 1; }));

    client.disconnect();
    EXPECT_FALSE(client.is_connected());

    server.stop();
}

TEST(TransportClient, ConnectNoServerFails) {
    vxl::TransportClient client;
    // Try connecting to a port where nothing is listening
    auto r = client.connect("127.0.0.1", 19999);
    EXPECT_FALSE(r.ok());
}

// ===========================================================================
// Request / Response
// ===========================================================================

TEST(Transport, RequestResponse) {
    vxl::TransportServer server;
    server.on_request("echo", [](const vxl::TransportMessage& req) -> vxl::TransportMessage {
        vxl::TransportMessage resp;
        resp.type = "response";
        resp.method = req.method;
        resp.payload = req.payload; // echo back
        return resp;
    });

    auto sr = server.start("127.0.0.1", 0);
    ASSERT_TRUE(sr.ok());

    vxl::TransportClient client;
    auto cr = client.connect("127.0.0.1", server.port());
    ASSERT_TRUE(cr.ok());

    // Give server time to accept
    EXPECT_TRUE(wait_until([&]() { return server.connected_clients() == 1; }));

    auto result = client.request("echo", "{\"msg\":\"hello\"}", 5000);
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(result.value.type, "response");
    EXPECT_EQ(result.value.method, "echo");
    EXPECT_EQ(result.value.payload, "{\"msg\":\"hello\"}");

    client.disconnect();
    server.stop();
}

TEST(Transport, UnknownMethodReturnsError) {
    vxl::TransportServer server;
    auto sr = server.start("127.0.0.1", 0);
    ASSERT_TRUE(sr.ok());

    vxl::TransportClient client;
    auto cr = client.connect("127.0.0.1", server.port());
    ASSERT_TRUE(cr.ok());

    EXPECT_TRUE(wait_until([&]() { return server.connected_clients() == 1; }));

    auto result = client.request("nonexistent", "{}", 5000);
    ASSERT_TRUE(result.ok()); // transport-level success
    EXPECT_EQ(result.value.payload, "{\"error\":\"unknown method\"}");

    client.disconnect();
    server.stop();
}

// ===========================================================================
// Broadcast events
// ===========================================================================

TEST(Transport, BroadcastEvent) {
    vxl::TransportServer server;
    auto sr = server.start("127.0.0.1", 0);
    ASSERT_TRUE(sr.ok());

    vxl::TransportClient client;
    auto cr = client.connect("127.0.0.1", server.port());
    ASSERT_TRUE(cr.ok());

    EXPECT_TRUE(wait_until([&]() { return server.connected_clients() == 1; }));

    std::mutex mu;
    std::condition_variable cv;
    vxl::TransportMessage received_event;
    bool got_event = false;

    client.on_event([&](const vxl::TransportMessage& event) {
        std::lock_guard<std::mutex> lock(mu);
        received_event = event;
        got_event = true;
        cv.notify_one();
    });

    vxl::TransportMessage event;
    event.type = "event";
    event.method = "status_update";
    event.payload = "{\"status\":\"ok\"}";
    event.id = 0;

    auto br = server.broadcast_event(event);
    ASSERT_TRUE(br.ok());

    // Wait for client to receive the event
    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&]() { return got_event; }));
    }

    EXPECT_EQ(received_event.type, "event");
    EXPECT_EQ(received_event.method, "status_update");
    EXPECT_EQ(received_event.payload, "{\"status\":\"ok\"}");

    client.disconnect();
    server.stop();
}

// ===========================================================================
// Timeout
// ===========================================================================

TEST(Transport, RequestTimeout) {
    // Use a handler with a cancellable sleep so server.stop() doesn't hang.
    std::atomic<bool> cancel{false};
    vxl::TransportServer server;
    server.on_request("slow", [&cancel](const vxl::TransportMessage& req) -> vxl::TransportMessage {
        // Sleep in small increments so we can cancel
        for (int i = 0; i < 50 && !cancel; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        vxl::TransportMessage resp;
        resp.type = "response";
        resp.method = req.method;
        resp.payload = "{}";
        return resp;
    });

    auto sr = server.start("127.0.0.1", 0);
    ASSERT_TRUE(sr.ok());

    vxl::TransportClient client;
    auto cr = client.connect("127.0.0.1", server.port());
    ASSERT_TRUE(cr.ok());

    EXPECT_TRUE(wait_until([&]() { return server.connected_clients() == 1; }));

    // Use a short timeout -- the handler sleeps much longer
    auto result = client.request("slow", "{}", 200);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, vxl::ErrorCode::DEVICE_TIMEOUT);

    cancel = true; // Signal the handler to stop sleeping
    client.disconnect();
    server.stop();
}

// ===========================================================================
// Multiple clients
// ===========================================================================

TEST(Transport, MultipleClients) {
    vxl::TransportServer server;
    server.on_request("ping", [](const vxl::TransportMessage& req) -> vxl::TransportMessage {
        vxl::TransportMessage resp;
        resp.type = "response";
        resp.method = "ping";
        resp.payload = "{\"pong\":true}";
        return resp;
    });

    auto sr = server.start("127.0.0.1", 0);
    ASSERT_TRUE(sr.ok());

    vxl::TransportClient client1, client2;
    ASSERT_TRUE(client1.connect("127.0.0.1", server.port()).ok());
    ASSERT_TRUE(client2.connect("127.0.0.1", server.port()).ok());

    EXPECT_TRUE(wait_until([&]() { return server.connected_clients() == 2; }));

    auto r1 = client1.request("ping", "{}", 5000);
    auto r2 = client2.request("ping", "{}", 5000);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r1.value.payload, "{\"pong\":true}");
    EXPECT_EQ(r2.value.payload, "{\"pong\":true}");

    client1.disconnect();
    client2.disconnect();
    server.stop();
}

// ===========================================================================
// Multiple sequential requests on the same connection
// ===========================================================================

TEST(Transport, MultipleRequests) {
    vxl::TransportServer server;
    std::atomic<int> counter{0};
    server.on_request("increment", [&counter](const vxl::TransportMessage& req) -> vxl::TransportMessage {
        int val = ++counter;
        vxl::TransportMessage resp;
        resp.type = "response";
        resp.method = req.method;
        resp.payload = "{\"count\":" + std::to_string(val) + "}";
        return resp;
    });

    auto sr = server.start("127.0.0.1", 0);
    ASSERT_TRUE(sr.ok());

    vxl::TransportClient client;
    ASSERT_TRUE(client.connect("127.0.0.1", server.port()).ok());
    EXPECT_TRUE(wait_until([&]() { return server.connected_clients() == 1; }));

    for (int i = 1; i <= 5; i++) {
        auto r = client.request("increment", "{}", 5000);
        ASSERT_TRUE(r.ok()) << "request " << i << " failed: " << r.message;
        EXPECT_EQ(r.value.method, "increment");
    }
    EXPECT_EQ(counter.load(), 5);

    client.disconnect();
    server.stop();
}
