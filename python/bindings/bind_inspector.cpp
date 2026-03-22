// VxlStudio Python bindings -- Inspector3D, InspectorConfig, InspectorResult
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include "vxl/inspector_3d.h"
#include "vxl/result.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_inspector(py::module_& m) {
    // ---- CompareResult -----------------------------------------------------
    py::class_<vxl::CompareResult>(m, "CompareResult")
        .def(py::init<>())
        .def_readwrite("diff_mask", &vxl::CompareResult::diff_mask)
        .def_readwrite("defects",   &vxl::CompareResult::defects)
        .def_readwrite("max_diff",  &vxl::CompareResult::max_diff)
        .def_readwrite("mean_diff", &vxl::CompareResult::mean_diff)
        .def("__repr__", [](const vxl::CompareResult& r) {
            return "CompareResult(max_diff=" + std::to_string(r.max_diff) +
                   ", mean_diff=" + std::to_string(r.mean_diff) +
                   ", defects=" + std::to_string(r.defects.size()) + ")";
        });

    // ---- InspectorConfig ---------------------------------------------------
    py::class_<vxl::InspectorConfig>(m, "InspectorConfig")
        .def(py::init<>())
        .def_readwrite("name",     &vxl::InspectorConfig::name)
        .def_readwrite("type",     &vxl::InspectorConfig::type)
        .def_readwrite("rois",     &vxl::InspectorConfig::rois)
        .def_readwrite("params",   &vxl::InspectorConfig::params)
        .def_readwrite("severity", &vxl::InspectorConfig::severity)
        .def("__repr__", [](const vxl::InspectorConfig& c) {
            return "InspectorConfig(name=" + c.name +
                   ", type=" + c.type +
                   ", severity=" + c.severity + ")";
        });

    // ---- InspectorResult ---------------------------------------------------
    py::class_<vxl::InspectorResult>(m, "InspectorResult")
        .def(py::init<>())
        .def_readwrite("name",     &vxl::InspectorResult::name)
        .def_readwrite("type",     &vxl::InspectorResult::type)
        .def_readwrite("severity", &vxl::InspectorResult::severity)
        .def_readwrite("pass_",    &vxl::InspectorResult::pass,
                       "Whether this individual inspector passed (Python name: pass_).")
        .def_readwrite("message",  &vxl::InspectorResult::message)
        .def_readwrite("measure",  &vxl::InspectorResult::measure)
        .def_readwrite("defects",  &vxl::InspectorResult::defects)
        .def("__repr__", [](const vxl::InspectorResult& r) {
            return std::string("InspectorResult(name=") + r.name +
                   ", pass=" + (r.pass ? "True" : "False") + ")";
        });

    // ---- build_inspection_result free function -----------------------------
    m.def("build_inspection_result",
        [](vxl::InspectionResult& out,
           const std::vector<vxl::InspectorResult>& per_inspector,
           const std::string& recipe_name) {
            vxl::build_inspection_result(out, per_inspector, recipe_name);
        },
        py::arg("out"), py::arg("per_inspector"), py::arg("recipe_name"),
        "Populate an InspectionResult from per-inspector results.");

    // ---- Inspector3D -------------------------------------------------------
    py::class_<vxl::Inspector3D>(m, "Inspector3D")
        .def(py::init<>())
        .def("set_reference", &vxl::Inspector3D::set_reference,
             py::arg("ref_hmap"),
             "Set a reference height map for differential inspection.")
        .def("add_inspector", &vxl::Inspector3D::add_inspector,
             py::arg("config"),
             "Add an inspector configuration.")
        .def("clear", &vxl::Inspector3D::clear,
             "Remove all inspector configurations.")
        .def("run", [](const vxl::Inspector3D& insp, const vxl::HeightMap& hmap) {
            vxl::Result<vxl::InspectionResult> r;
            {
                py::gil_scoped_release release;
                r = insp.run(hmap);
            }
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("hmap"),
           "Run all configured inspectors on the height map.")
        // ---- static algorithm functions ----
        .def_static("ref_plane_fit",
            [](const vxl::HeightMap& hmap, const vxl::ROI& roi) {
                auto r = vxl::Inspector3D::ref_plane_fit(hmap, roi);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("roi"),
            "Fit a reference plane in the given ROI.")
        .def_static("height_measure",
            [](const vxl::HeightMap& hmap, const vxl::ROI& roi, double ref_height) {
                auto r = vxl::Inspector3D::height_measure(hmap, roi, ref_height);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("roi"), py::arg("ref_height") = 0.0,
            "Compute height statistics in the ROI.")
        .def_static("flatness",
            [](const vxl::HeightMap& hmap, const vxl::ROI& roi) {
                auto r = vxl::Inspector3D::flatness(hmap, roi);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("roi"),
            "Compute flatness (max deviation from best-fit plane).")
        .def_static("height_threshold",
            [](const vxl::HeightMap& hmap, float min_h, float max_h) {
                auto r = vxl::Inspector3D::height_threshold(hmap, min_h, max_h);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("min_h"), py::arg("max_h"),
            "Binary threshold on height values. Returns a GRAY8 mask Image.")
        .def_static("defect_cluster",
            [](const vxl::Image& binary_mask, float resolution_mm, int min_area_pixels) {
                auto r = vxl::Inspector3D::defect_cluster(
                    binary_mask, resolution_mm, min_area_pixels);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("binary_mask"), py::arg("resolution_mm"),
            py::arg("min_area_pixels") = 10,
            "Connected-component clustering on a binary mask.")
        .def_static("coplanarity",
            [](const vxl::HeightMap& hmap, const std::vector<vxl::ROI>& rois) {
                auto r = vxl::Inspector3D::coplanarity(hmap, rois);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("rois"),
            "Measure max deviation from best-fit plane across ROI centers.")
        .def_static("template_compare",
            [](const vxl::HeightMap& current, const vxl::HeightMap& reference,
               float threshold_mm, int min_area_pixels) {
                auto r = vxl::Inspector3D::template_compare(
                    current, reference, threshold_mm, min_area_pixels);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("current"), py::arg("reference"),
            py::arg("threshold_mm") = 0.1f,
            py::arg("min_area_pixels") = 10,
            "Compare current vs reference height map, return CompareResult.");
}
