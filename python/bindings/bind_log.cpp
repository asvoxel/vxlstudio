// VxlStudio Python bindings -- logging
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "vxl/log.h"

namespace py = pybind11;

void init_log(py::module_& m) {
    auto log_m = m.def_submodule("log", "VxlStudio logging subsystem");

    py::enum_<vxl::log::Level>(log_m, "Level")
        .value("TRACE", vxl::log::Level::TRACE)
        .value("DEBUG", vxl::log::Level::DEBUG)
        .value("INFO",  vxl::log::Level::INFO)
        .value("WARN",  vxl::log::Level::WARN)
        .value("ERROR", vxl::log::Level::ERROR)
        .value("FATAL", vxl::log::Level::FATAL)
        .export_values();

    log_m.def("init", &vxl::log::init,
              "Initialize the logging subsystem.");
    log_m.def("set_level", &vxl::log::set_level, py::arg("level"),
              "Set the minimum log level.");

    log_m.def("trace", &vxl::log::trace, py::arg("msg"));
    log_m.def("debug", &vxl::log::debug, py::arg("msg"));
    log_m.def("info",  &vxl::log::info,  py::arg("msg"));
    log_m.def("warn",  &vxl::log::warn,  py::arg("msg"));
    log_m.def("error", &vxl::log::error, py::arg("msg"));
    log_m.def("fatal", &vxl::log::fatal, py::arg("msg"));

    log_m.def("add_console_sink", &vxl::log::add_console_sink,
              "Add stdout/stderr console sink.");
    log_m.def("add_file_sink", &vxl::log::add_file_sink, py::arg("path"),
              "Add a file sink that writes to the given path.");

    // add_callback_sink: wrap the Python callable so GIL is acquired
    log_m.def("add_callback_sink",
        [](py::object callback) {
            // Prevent the GIL-free C++ callback thread from crashing:
            // we acquire the GIL before calling into Python.
            vxl::log::add_callback_sink(
                [callback](vxl::log::Level level, const std::string& msg) {
                    py::gil_scoped_acquire gil;
                    callback(level, msg);
                });
        },
        py::arg("callback"),
        "Add a callback sink. The callable receives (Level, str).");

    log_m.def("save_image", &vxl::log::save_image,
              py::arg("image"), py::arg("tag"),
              "Persist an Image to the log directory with the given tag.");
    log_m.def("save_height_map", &vxl::log::save_height_map,
              py::arg("height_map"), py::arg("tag"),
              "Persist a HeightMap to the log directory with the given tag.");
    log_m.def("save_result", &vxl::log::save_result,
              py::arg("result"),
              "Persist an InspectionResult as JSON to the log directory.");

    log_m.def("set_log_dir", &vxl::log::set_log_dir, py::arg("dir"),
              "Set the directory for log files.");
    log_m.def("set_max_days", &vxl::log::set_max_days, py::arg("days"),
              "Set the maximum number of days to keep log files.");
    log_m.def("set_max_size_mb", &vxl::log::set_max_size_mb, py::arg("size_mb"),
              "Set the maximum total log size in megabytes.");
}
