// VxlStudio Python bindings -- MessageBus
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "vxl/message_bus.h"

namespace py = pybind11;

void init_message_bus(py::module_& m) {
    // ---- Message base and predefined subtypes ------------------------------
    py::class_<vxl::Message, std::shared_ptr<vxl::Message>>(m, "Message")
        .def(py::init<>())
        .def_readwrite("topic",        &vxl::Message::topic)
        .def_readwrite("timestamp_ns", &vxl::Message::timestamp_ns);

    py::class_<vxl::FrameCaptured, vxl::Message,
               std::shared_ptr<vxl::FrameCaptured>>(m, "FrameCaptured")
        .def(py::init<>())
        .def_readwrite("image",     &vxl::FrameCaptured::image)
        .def_readwrite("camera_id", &vxl::FrameCaptured::camera_id);

    py::class_<vxl::ReconstructDone, vxl::Message,
               std::shared_ptr<vxl::ReconstructDone>>(m, "ReconstructDone")
        .def(py::init<>())
        .def_readwrite("height_map",  &vxl::ReconstructDone::height_map)
        .def_readwrite("point_cloud", &vxl::ReconstructDone::point_cloud);

    py::class_<vxl::InspectionDone, vxl::Message,
               std::shared_ptr<vxl::InspectionDone>>(m, "InspectionDone")
        .def(py::init<>())
        .def_readwrite("result", &vxl::InspectionDone::result);

    py::class_<vxl::ParamChanged, vxl::Message,
               std::shared_ptr<vxl::ParamChanged>>(m, "ParamChanged")
        .def(py::init<>())
        .def_readwrite("key",   &vxl::ParamChanged::key)
        .def_readwrite("value", &vxl::ParamChanged::value);

    py::class_<vxl::AlarmTriggered, vxl::Message,
               std::shared_ptr<vxl::AlarmTriggered>>(m, "AlarmTriggered")
        .def(py::init<>())
        .def_readwrite("alarm_id",    &vxl::AlarmTriggered::alarm_id)
        .def_readwrite("description", &vxl::AlarmTriggered::description);

    // ---- DispatchMode ------------------------------------------------------
    py::enum_<vxl::DispatchMode>(m, "DispatchMode")
        .value("SYNC", vxl::DispatchMode::SYNC)
        .export_values();

    // ---- MessageBus --------------------------------------------------------
    py::class_<vxl::MessageBus>(m, "MessageBus")
        .def(py::init<>())
        .def("publish", &vxl::MessageBus::publish,
             py::arg("msg"),
             "Publish a message to all matching subscribers.")
        .def("subscribe",
            [](vxl::MessageBus& bus, const std::string& topic, py::object handler) {
                // Wrap the Python callable so GIL is acquired before entering Python
                return bus.subscribe(topic,
                    [handler](const std::shared_ptr<vxl::Message>& msg) {
                        py::gil_scoped_acquire gil;
                        handler(msg);
                    });
            },
            py::arg("topic"), py::arg("handler"),
            "Subscribe to a topic. handler receives a Message (shared_ptr).")
        .def("unsubscribe", &vxl::MessageBus::unsubscribe,
             py::arg("id"),
             "Unsubscribe a handler by its subscription id.");

    // Global accessor
    m.def("message_bus", &vxl::message_bus,
          py::return_value_policy::reference,
          "Return the global MessageBus singleton.");
}
