// VxlStudio Python bindings -- Plane, HeightMapProcessor
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/height_map.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_height_map(py::module_& m) {
    // ---- Plane -------------------------------------------------------------
    py::class_<vxl::Plane>(m, "Plane")
        .def(py::init<>())
        .def(py::init([](double a, double b, double c, double d) {
            vxl::Plane p; p.a = a; p.b = b; p.c = c; p.d = d; return p;
        }), py::arg("a") = 0.0, py::arg("b") = 0.0,
            py::arg("c") = 1.0, py::arg("d") = 0.0)
        .def_readwrite("a", &vxl::Plane::a)
        .def_readwrite("b", &vxl::Plane::b)
        .def_readwrite("c", &vxl::Plane::c)
        .def_readwrite("d", &vxl::Plane::d)
        .def("distance", &vxl::Plane::distance,
             py::arg("x"), py::arg("y"), py::arg("z"),
             "Signed distance from point (x, y, z) to this plane.")
        .def("__repr__", [](const vxl::Plane& p) {
            return "Plane(a=" + std::to_string(p.a) +
                   ", b=" + std::to_string(p.b) +
                   ", c=" + std::to_string(p.c) +
                   ", d=" + std::to_string(p.d) + ")";
        });

    // ---- HeightMapProcessor (static utility methods) -----------------------
    py::class_<vxl::HeightMapProcessor>(m, "HeightMapProcessor")
        .def_static("fit_reference_plane",
            [](const vxl::HeightMap& hmap, const vxl::ROI& roi) {
                auto r = vxl::HeightMapProcessor::fit_reference_plane(hmap, roi);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("roi"),
            "Fit a reference plane via least-squares in the given ROI.")
        .def_static("subtract_reference",
            [](const vxl::HeightMap& hmap, const vxl::Plane& plane) {
                auto r = vxl::HeightMapProcessor::subtract_reference(hmap, plane);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("plane"),
            "Subtract a reference plane from the height map.")
        .def_static("apply_filter",
            [](const vxl::HeightMap& hmap, const std::string& type, int kernel_size) {
                auto r = vxl::HeightMapProcessor::apply_filter(hmap, type, kernel_size);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("type"), py::arg("kernel_size"),
            "Apply a spatial filter ('median' or 'gaussian').")
        .def_static("crop_roi",
            &vxl::HeightMapProcessor::crop_roi,
            py::arg("hmap"), py::arg("roi"),
            "Extract a sub-region defined by the ROI.")
        .def_static("interpolate_holes",
            [](const vxl::HeightMap& hmap) {
                auto r = vxl::HeightMapProcessor::interpolate_holes(hmap);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"),
            "Fill NaN pixels using nearest-neighbor BFS.");
}
