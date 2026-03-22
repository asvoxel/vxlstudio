// VxlStudio Python bindings -- Camera
// SPDX-License-Identifier: MIT

#include <memory>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/camera.h"
#include "vxl/camera_manager.h"

namespace py = pybind11;

// Helper declared in bind_error.cpp
extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_camera(py::module_& m) {
    // ---- ICamera (abstract base) -------------------------------------------
    py::class_<vxl::ICamera>(m, "ICamera")
        .def("device_id", &vxl::ICamera::device_id)
        .def("open", [](vxl::ICamera& cam) {
            auto r = cam.open();
            throw_on_error(r.code, r.message);
        }, "Open the camera device. Raises DeviceError on failure.")
        .def("close", &vxl::ICamera::close)
        .def("is_open", &vxl::ICamera::is_open);

    // ---- ICamera2D ---------------------------------------------------------
    py::class_<vxl::ICamera2D, vxl::ICamera,
               std::unique_ptr<vxl::ICamera2D>>(m, "ICamera2D")
        .def("capture", [](vxl::ICamera2D& cam) {
            auto r = cam.capture();
            throw_on_error(r.code, r.message);
            return r.value;
        }, "Capture a single 2D image. Raises DeviceError on failure.")
        .def("set_exposure", [](vxl::ICamera2D& cam, int us) {
            auto r = cam.set_exposure(us);
            throw_on_error(r.code, r.message);
        }, py::arg("us"), "Set exposure time in microseconds.")
        .def("exposure", &vxl::ICamera2D::exposure,
             "Return current exposure time in microseconds.");

    // ---- ICamera3D ---------------------------------------------------------
    py::class_<vxl::ICamera3D, vxl::ICamera,
               std::unique_ptr<vxl::ICamera3D>>(m, "ICamera3D")
        .def("capture_sequence", [](vxl::ICamera3D& cam) {
            vxl::Result<std::vector<vxl::Image>> r;
            {
                py::gil_scoped_release release;
                r = cam.capture_sequence();
            }
            throw_on_error(r.code, r.message);
            return r.value;
        }, "Capture the fringe-pattern sequence. Raises DeviceError on failure.")
        .def("set_exposure", [](vxl::ICamera3D& cam, int us) {
            auto r = cam.set_exposure(us);
            throw_on_error(r.code, r.message);
        }, py::arg("us"), "Set exposure time in microseconds.")
        .def("exposure", &vxl::ICamera3D::exposure,
             "Return current exposure time in microseconds.")
        .def("set_fringe_count", [](vxl::ICamera3D& cam, int count) {
            auto r = cam.set_fringe_count(count);
            throw_on_error(r.code, r.message);
        }, py::arg("count"), "Set the number of fringe frequencies.")
        .def("fringe_count", &vxl::ICamera3D::fringe_count,
             "Return current fringe count.");

    // ---- Camera factory namespace ------------------------------------------
    auto cam_m = m.def_submodule("Camera", "Camera factory functions");

    cam_m.def("open_3d",
        [](const std::string& device_id) {
            auto r = vxl::Camera::open_3d(device_id);
            throw_on_error(r.code, r.message);
            return std::move(r.value);
        },
        py::arg("device_id"),
        "Open a 3D structured-light camera by device id. Raises DeviceError on failure.");

    cam_m.def("open_2d",
        [](const std::string& device_id) {
            auto r = vxl::Camera::open_2d(device_id);
            throw_on_error(r.code, r.message);
            return std::move(r.value);
        },
        py::arg("device_id"),
        "Open a 2D camera by device id. Raises DeviceError on failure.");

    cam_m.def("enumerate", &vxl::Camera::enumerate,
              "Return a list of available camera device ids.");

    // ---- CameraManager -----------------------------------------------------
    py::class_<vxl::CameraManager>(m, "CameraManager")
        .def(py::init<>())
        .def("add_camera",
            [](vxl::CameraManager& mgr, const std::string& device_id,
               const vxl::CalibrationParams& calib) {
                auto r = mgr.add_camera(device_id, calib);
                throw_on_error(r.code, r.message);
            },
            py::arg("device_id"), py::arg("calib"),
            "Add a 3D camera with calibration parameters.")
        .def("remove_camera",
            [](vxl::CameraManager& mgr, const std::string& device_id) {
                auto r = mgr.remove_camera(device_id);
                throw_on_error(r.code, r.message);
            },
            py::arg("device_id"),
            "Remove a camera by device id.")
        .def("camera_ids", &vxl::CameraManager::camera_ids,
             "List all managed camera device ids.")
        .def("get_camera", &vxl::CameraManager::get_camera,
             py::arg("device_id"),
             py::return_value_policy::reference_internal,
             "Get a camera pointer by device id (None if not found).")
        .def("get_calibration", &vxl::CameraManager::get_calibration,
             py::arg("device_id"),
             py::return_value_policy::reference_internal,
             "Get calibration parameters for a camera.")
        .def("capture_all",
            [](vxl::CameraManager& mgr) {
                decltype(mgr.capture_all()) r;
                {
                    py::gil_scoped_release release;
                    r = mgr.capture_all();
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            "Capture from all cameras sequentially.")
        .def_static("aggregate_results",
            &vxl::CameraManager::aggregate_results,
            py::arg("per_camera_results"),
            "Aggregate per-camera inspection results (any NG => overall NG).");
}
