// VxlStudio Python bindings -- Guidance (robot grasp planning)
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/guidance.h"

namespace py = pybind11;

// Helper declared in bind_error.cpp
extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_guidance(py::module_& m) {
    // ---- GraspPose -----------------------------------------------------------
    py::class_<vxl::GraspPose>(m, "GraspPose")
        .def(py::init<>())
        .def_readwrite("pose",     &vxl::GraspPose::pose)
        .def_readwrite("score",    &vxl::GraspPose::score)
        .def_readwrite("width_mm", &vxl::GraspPose::width_mm)
        .def_readwrite("label",    &vxl::GraspPose::label)
        .def("__repr__", [](const vxl::GraspPose& g) {
            return "GraspPose(score=" + std::to_string(g.score) +
                   ", label='" + g.label + "')";
        });

    // ---- GuidanceParams ------------------------------------------------------
    py::class_<vxl::GuidanceParams>(m, "GuidanceParams")
        .def(py::init<>())
        .def_readwrite("strategy",             &vxl::GuidanceParams::strategy)
        .def_readwrite("approach_distance_mm", &vxl::GuidanceParams::approach_distance_mm)
        .def_readwrite("min_score",            &vxl::GuidanceParams::min_score)
        .def_readwrite("max_results",          &vxl::GuidanceParams::max_results)
        .def_readwrite("object_height_mm",     &vxl::GuidanceParams::object_height_mm)
        .def("__repr__", [](const vxl::GuidanceParams& p) {
            return "GuidanceParams(strategy='" + p.strategy +
                   "', max_results=" + std::to_string(p.max_results) + ")";
        });

    // ---- GuidanceEngine ------------------------------------------------------
    py::class_<vxl::GuidanceEngine>(m, "GuidanceEngine")
        .def(py::init<>())
        .def("compute_grasp",
            [](vxl::GuidanceEngine& eng,
               const vxl::PointCloud& cloud,
               const vxl::GuidanceParams& params) {
                vxl::Result<std::vector<vxl::GraspPose>> r;
                {
                    py::gil_scoped_release release;
                    r = eng.compute_grasp(cloud, params);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("cloud"),
            py::arg("params") = vxl::GuidanceParams{},
            "Compute grasp poses from a 3D point cloud.")
        .def("compute_grasp_2d",
            [](vxl::GuidanceEngine& eng,
               const vxl::HeightMap& hmap,
               const vxl::GuidanceParams& params) {
                vxl::Result<std::vector<vxl::GraspPose>> r;
                {
                    py::gil_scoped_release release;
                    r = eng.compute_grasp_2d(hmap, params);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"),
            py::arg("params") = vxl::GuidanceParams{},
            "Compute grasp poses from a 2.5D height map.")
        .def_static("find_pick_point",
            [](const vxl::HeightMap& hmap, const vxl::ROI& roi) {
                auto r = vxl::GuidanceEngine::find_pick_point(hmap, roi);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("hmap"), py::arg("roi"),
            "Find the highest point in a HeightMap ROI and return it as a GraspPose.")
        .def_static("camera_to_robot",
            [](const vxl::Pose6D& camera_pose,
               const vxl::Pose6D& hand_eye_transform) {
                auto r = vxl::GuidanceEngine::camera_to_robot(
                    camera_pose, hand_eye_transform);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("camera_pose"), py::arg("hand_eye_transform"),
            "Transform a pose from camera frame to robot frame via hand-eye calibration.");
}
