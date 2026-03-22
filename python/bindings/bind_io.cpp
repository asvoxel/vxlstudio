// VxlStudio Python bindings -- IO / PLC
// SPDX-License-Identifier: MIT

#include <memory>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/io.h"

namespace py = pybind11;

// Helper declared in bind_error.cpp
extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_io(py::module_& m) {
    // ---- IIO (abstract base) -----------------------------------------------
    py::class_<vxl::IIO, std::unique_ptr<vxl::IIO>>(m, "IIO")
        .def("device_id", &vxl::IIO::device_id,
             "Return the device identifier string.")
        .def("open", [](vxl::IIO& io) {
            auto r = io.open();
            throw_on_error(r.code, r.message);
        }, "Open the IO device. Raises DeviceError on failure.")
        .def("close", &vxl::IIO::close,
             "Close the IO device.")
        .def("is_open", &vxl::IIO::is_open,
             "Return True if the device is open.")
        .def("set_output", [](vxl::IIO& io, const std::string& pin, bool value) {
            auto r = io.set_output(pin, value);
            throw_on_error(r.code, r.message);
        }, py::arg("pin"), py::arg("value"),
           "Set a digital output pin. Raises IOError_ on failure.")
        .def("get_input", [](vxl::IIO& io, const std::string& pin) {
            auto r = io.get_input(pin);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("pin"),
           "Read a digital input pin. Returns bool. Raises IOError_ on failure.")
        .def("write_register", [](vxl::IIO& io, int address, int value) {
            auto r = io.write_register(address, static_cast<uint16_t>(value));
            throw_on_error(r.code, r.message);
        }, py::arg("address"), py::arg("value"),
           "Write a 16-bit register. Raises IOError_ on failure.")
        .def("read_register", [](vxl::IIO& io, int address) {
            auto r = io.read_register(address);
            throw_on_error(r.code, r.message);
            return static_cast<int>(r.value);
        }, py::arg("address"),
           "Read a 16-bit register. Returns int. Raises IOError_ on failure.");

    // ---- ITrigger ----------------------------------------------------------
    py::class_<vxl::ITrigger>(m, "ITrigger")
        .def("wait_trigger", [](vxl::ITrigger& trig, int timeout_ms) {
            vxl::Result<void> r;
            {
                py::gil_scoped_release release;
                r = trig.wait_trigger(timeout_ms);
            }
            throw_on_error(r.code, r.message);
        }, py::arg("timeout_ms") = -1,
           "Wait for an external trigger. Releases the GIL while waiting. "
           "Raises DeviceError on timeout or failure.")
        .def("send_trigger", [](vxl::ITrigger& trig) {
            auto r = trig.send_trigger();
            throw_on_error(r.code, r.message);
        }, "Send a software trigger. Raises DeviceError on failure.");

    // ---- IOManager (factory) -----------------------------------------------
    py::class_<vxl::IOManager>(m, "IOManager")
        .def_static("open", [](const std::string& uri) {
            auto r = vxl::IOManager::open(uri);
            throw_on_error(r.code, r.message);
            return std::move(r.value);
        }, py::arg("uri"),
           "Open an IO device by URI. Returns IIO. Raises DeviceError on failure.\n"
           "URI formats: 'modbus-tcp://host:port', 'serial:///dev/ttyUSB0:9600', 'sim://io'")
        .def_static("enumerate", &vxl::IOManager::enumerate,
                    "Return a list of available IO device URIs.");
}
