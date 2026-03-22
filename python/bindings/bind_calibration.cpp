// VxlStudio Python bindings -- CalibrationParams + StereoCalibrator
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "vxl/calibration.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

// Helper macro to expose a fixed-size C array as a numpy property.
// The returned array shares memory with the struct (zero-copy, kept alive
// by holding a reference to the owning Python object).
#define DEF_ARRAY_PROP(cls, name, field, rows, cols)                           \
    cls.def_property(                                                          \
        name,                                                                  \
        [](py::object self) {                                                  \
            auto& obj = self.cast<vxl::CalibrationParams&>();                  \
            return py::array_t<double>(                                        \
                {static_cast<ssize_t>(rows), static_cast<ssize_t>(cols)},      \
                {static_cast<ssize_t>(cols * sizeof(double)),                  \
                 static_cast<ssize_t>(sizeof(double))},                        \
                obj.field, self);                                              \
        },                                                                     \
        [](vxl::CalibrationParams& obj, py::array_t<double> arr) {            \
            auto r = arr.unchecked<2>();                                        \
            if (r.shape(0) != rows || r.shape(1) != cols)                      \
                throw std::invalid_argument(                                   \
                    std::string(name) + " must be " +                          \
                    std::to_string(rows) + "x" + std::to_string(cols));        \
            for (ssize_t i = 0; i < rows; ++i)                                \
                for (ssize_t j = 0; j < cols; ++j)                            \
                    obj.field[i * cols + j] = r(i, j);                         \
        })

#define DEF_VEC_PROP(cls, name, field, len)                                    \
    cls.def_property(                                                          \
        name,                                                                  \
        [](py::object self) {                                                  \
            auto& obj = self.cast<vxl::CalibrationParams&>();                  \
            return py::array_t<double>(                                        \
                {static_cast<ssize_t>(len)},                                   \
                {static_cast<ssize_t>(sizeof(double))},                        \
                obj.field, self);                                              \
        },                                                                     \
        [](vxl::CalibrationParams& obj, py::array_t<double> arr) {            \
            auto r = arr.unchecked<1>();                                        \
            if (r.shape(0) != len)                                             \
                throw std::invalid_argument(                                   \
                    std::string(name) + " must have " +                        \
                    std::to_string(len) + " elements");                        \
            for (ssize_t i = 0; i < len; ++i)                                 \
                obj.field[i] = r(i);                                           \
        })

void init_calibration(py::module_& m) {
    // ---- CalibrationParams --------------------------------------------------
    auto cls = py::class_<vxl::CalibrationParams>(m, "CalibrationParams")
        .def(py::init<>())
        .def_readwrite("image_width",      &vxl::CalibrationParams::image_width)
        .def_readwrite("image_height",     &vxl::CalibrationParams::image_height)
        .def_readwrite("projector_width",  &vxl::CalibrationParams::projector_width)
        .def_readwrite("projector_height", &vxl::CalibrationParams::projector_height)
        .def("save", [](const vxl::CalibrationParams& self, const std::string& path) {
            auto r = self.save(path);
            throw_on_error(r.code, r.message);
        }, py::arg("path"), "Save calibration to a file.")
        .def_static("load", [](const std::string& path) {
            auto r = vxl::CalibrationParams::load(path);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("path"), "Load calibration from a file.")
        .def_static("default_sim", &vxl::CalibrationParams::default_sim,
                    "Return default calibration parameters for the simulator.")
        .def("__repr__", [](const vxl::CalibrationParams& c) {
            return "CalibrationParams(image=" + std::to_string(c.image_width) +
                   "x" + std::to_string(c.image_height) +
                   ", proj=" + std::to_string(c.projector_width) +
                   "x" + std::to_string(c.projector_height) + ")";
        });

    // Expose arrays as numpy views.
    DEF_ARRAY_PROP(cls, "camera_matrix",          camera_matrix,          3, 3);
    DEF_VEC_PROP  (cls, "camera_distortion",      camera_distortion,      5);
    DEF_ARRAY_PROP(cls, "projector_matrix",       projector_matrix,       3, 3);
    DEF_VEC_PROP  (cls, "projector_distortion",   projector_distortion,   5);
    DEF_ARRAY_PROP(cls, "rotation",               rotation,               3, 3);
    DEF_VEC_PROP  (cls, "translation",            translation,            3);

    // ---- BoardParams --------------------------------------------------------
    py::class_<vxl::BoardParams>(m, "BoardParams")
        .def(py::init<>())
        .def_readwrite("cols",           &vxl::BoardParams::cols)
        .def_readwrite("rows",           &vxl::BoardParams::rows)
        .def_readwrite("square_size_mm", &vxl::BoardParams::square_size_mm)
        .def("__repr__", [](const vxl::BoardParams& b) {
            return "BoardParams(cols=" + std::to_string(b.cols) +
                   ", rows=" + std::to_string(b.rows) +
                   ", square_size_mm=" + std::to_string(b.square_size_mm) + ")";
        });

    // ---- CalibrationResult --------------------------------------------------
    py::class_<vxl::CalibrationResult>(m, "CalibrationResult")
        .def(py::init<>())
        .def_readwrite("params",             &vxl::CalibrationResult::params)
        .def_readwrite("reprojection_error", &vxl::CalibrationResult::reprojection_error)
        .def_readwrite("image_pairs_used",   &vxl::CalibrationResult::image_pairs_used)
        .def("__repr__", [](const vxl::CalibrationResult& r) {
            return "CalibrationResult(rms=" +
                   std::to_string(r.reprojection_error) +
                   ", pairs=" + std::to_string(r.image_pairs_used) + ")";
        });

    // ---- StereoCalibrator ---------------------------------------------------
    py::class_<vxl::StereoCalibrator>(m, "StereoCalibrator")
        .def(py::init<>())
        .def("set_board", &vxl::StereoCalibrator::set_board,
             py::arg("board"), "Set checkerboard parameters.")
        .def("add_image_pair", [](vxl::StereoCalibrator& self,
                                   const vxl::Image& left,
                                   const vxl::Image& right) {
            auto r = self.add_image_pair(left, right);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("left"), py::arg("right"),
           "Add an image pair. Returns True if corners were found in both.")
        .def("add_image", [](vxl::StereoCalibrator& self,
                              const vxl::Image& image) {
            auto r = self.add_image(image);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("image"),
           "Add a single image. Returns True if corners were found.")
        .def("pair_count",  &vxl::StereoCalibrator::pair_count)
        .def("image_count", &vxl::StereoCalibrator::image_count)
        .def("ready",       &vxl::StereoCalibrator::ready)
        .def("calibrate_stereo", [](vxl::StereoCalibrator& self) {
            vxl::Result<vxl::CalibrationResult> r;
            {
                py::gil_scoped_release release;
                r = self.calibrate_stereo();
            }
            throw_on_error(r.code, r.message);
            return r.value;
        }, "Run stereo calibration. GIL is released during computation.")
        .def("calibrate_single", [](vxl::StereoCalibrator& self) {
            vxl::Result<vxl::CalibrationResult> r;
            {
                py::gil_scoped_release release;
                r = self.calibrate_single();
            }
            throw_on_error(r.code, r.message);
            return r.value;
        }, "Run single camera calibration. GIL is released during computation.")
        .def_static("detect_and_draw_corners",
            [](const vxl::Image& image, const vxl::BoardParams& board) {
                auto r = vxl::StereoCalibrator::detect_and_draw_corners(image, board);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("board"),
            "Detect corners and return annotated image.")
        .def("clear", &vxl::StereoCalibrator::clear);
}

#undef DEF_ARRAY_PROP
#undef DEF_VEC_PROP
