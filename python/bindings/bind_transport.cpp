// VxlStudio Python bindings -- Transport (JSON-over-TCP)
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "vxl/transport.h"

namespace py = pybind11;

// Helper declared in bind_error.cpp
extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_transport(py::module_& m) {
    // ---- TransportMessage -----------------------------------------------------
    py::class_<vxl::TransportMessage>(m, "TransportMessage")
        .def(py::init<>())
        .def_readwrite("type",    &vxl::TransportMessage::type)
        .def_readwrite("method",  &vxl::TransportMessage::method)
        .def_readwrite("payload", &vxl::TransportMessage::payload)
        .def_readwrite("id",      &vxl::TransportMessage::id)
        .def("__repr__", [](const vxl::TransportMessage& msg) {
            return "<TransportMessage type='" + msg.type +
                   "' method='" + msg.method +
                   "' id=" + std::to_string(msg.id) + ">";
        });

    // ---- TransportServer ------------------------------------------------------
    py::class_<vxl::TransportServer>(m, "TransportServer")
        .def(py::init<>())
        .def("start", [](vxl::TransportServer& srv, const std::string& address, int port) {
            auto r = srv.start(address, port);
            throw_on_error(r.code, r.message);
        }, py::arg("address") = "0.0.0.0", py::arg("port") = 0,
           "Start listening on address:port. Use port=0 for an ephemeral port.")
        .def("stop", &vxl::TransportServer::stop,
             "Stop the server and close all connections.")
        .def("is_running", &vxl::TransportServer::is_running,
             "Return True if the server is running.")
        .def("on_request", [](vxl::TransportServer& srv, const std::string& method,
                              py::function handler) {
            // Copy the py::function into a shared_ptr so it stays alive
            auto py_handler = std::make_shared<py::function>(handler);
            srv.on_request(method, [py_handler](const vxl::TransportMessage& req)
                -> vxl::TransportMessage {
                py::gil_scoped_acquire acquire;
                py::object result = (*py_handler)(req);
                return result.cast<vxl::TransportMessage>();
            });
        }, py::arg("method"), py::arg("handler"),
           "Register a handler for a given method name. "
           "Handler receives TransportMessage and must return TransportMessage.")
        .def("broadcast_event", [](vxl::TransportServer& srv, const vxl::TransportMessage& event) {
            auto r = srv.broadcast_event(event);
            throw_on_error(r.code, r.message);
        }, py::arg("event"),
           "Broadcast an event to all connected clients.")
        .def("connected_clients", &vxl::TransportServer::connected_clients,
             "Return the number of currently connected clients.")
        .def_property_readonly("address", &vxl::TransportServer::address,
                               "The address the server is bound to.")
        .def_property_readonly("port", &vxl::TransportServer::port,
                               "The port the server is bound to.");

    // ---- TransportClient ------------------------------------------------------
    py::class_<vxl::TransportClient>(m, "TransportClient")
        .def(py::init<>())
        .def("connect", [](vxl::TransportClient& cli, const std::string& address, int port) {
            auto r = cli.connect(address, port);
            throw_on_error(r.code, r.message);
        }, py::arg("address") = "127.0.0.1", py::arg("port"),
           "Connect to a TransportServer at address:port.")
        .def("disconnect", &vxl::TransportClient::disconnect,
             "Disconnect from the server.")
        .def("is_connected", &vxl::TransportClient::is_connected,
             "Return True if connected to a server.")
        .def("request", [](vxl::TransportClient& cli, const std::string& method,
                           const std::string& payload_json, int timeout_ms)
             -> vxl::TransportMessage {
            vxl::Result<vxl::TransportMessage> r;
            {
                py::gil_scoped_release release;
                r = cli.request(method, payload_json, timeout_ms);
            }
            throw_on_error(r.code, r.message);
            return std::move(r.value);
        }, py::arg("method"), py::arg("payload_json") = "{}",
           py::arg("timeout_ms") = 5000,
           "Send a request and wait for a response. Releases the GIL while waiting.")
        .def("on_event", [](vxl::TransportClient& cli, py::function callback) {
            auto py_cb = std::make_shared<py::function>(callback);
            cli.on_event([py_cb](const vxl::TransportMessage& event) {
                py::gil_scoped_acquire acquire;
                (*py_cb)(event);
            });
        }, py::arg("callback"),
           "Register a callback for server-sent events.");
}
