// VxlStudio Python bindings -- main module entry point
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>

namespace py = pybind11;

// Forward-declared init functions from each bind_*.cpp file.
void init_error(py::module_& m);
void init_types(py::module_& m);
void init_log(py::module_& m);
void init_message_bus(py::module_& m);
void init_camera(py::module_& m);
void init_calibration(py::module_& m);
void init_reconstruct(py::module_& m);
void init_height_map(py::module_& m);
void init_inspector(py::module_& m);
void init_point_cloud(py::module_& m);
void init_recipe(py::module_& m);
void init_inspector_2d(py::module_& m);
void init_inference(py::module_& m);
void init_pipeline(py::module_& m);
void init_io(py::module_& m);
void init_audit(py::module_& m);
void init_plugin(py::module_& m);
void init_vlm(py::module_& m);
void init_transport(py::module_& m);
void init_guidance(py::module_& m);
void init_visual_ai(py::module_& m);

PYBIND11_MODULE(_vxl_core, m) {
    m.doc() = "VxlStudio Core -- Python bindings for structured-light 3D inspection";

    // Order matters: error & types first (they define base types used by the rest).
    init_error(m);
    init_types(m);
    init_log(m);
    init_message_bus(m);
    init_camera(m);
    init_calibration(m);
    init_reconstruct(m);
    init_height_map(m);
    init_inspector(m);
    init_point_cloud(m);
    init_recipe(m);
    init_inspector_2d(m);
    init_inference(m);
    init_pipeline(m);
    init_io(m);
    init_audit(m);
    init_plugin(m);
    init_vlm(m);
    init_transport(m);
    init_guidance(m);
    init_visual_ai(m);
}
