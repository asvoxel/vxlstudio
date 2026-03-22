// VxlStudio Python bindings -- ErrorCode, Result, exceptions
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "vxl/error.h"

namespace py = pybind11;

// ---------------------------------------------------------------------------
// Python exception hierarchy mirroring VxlStudio error categories
// ---------------------------------------------------------------------------
static py::exception<std::runtime_error> *exc_vxl          = nullptr;
static py::exception<std::runtime_error> *exc_device       = nullptr;
static py::exception<std::runtime_error> *exc_calibration  = nullptr;
static py::exception<std::runtime_error> *exc_reconstruct  = nullptr;
static py::exception<std::runtime_error> *exc_inspect      = nullptr;
static py::exception<std::runtime_error> *exc_model        = nullptr;
static py::exception<std::runtime_error> *exc_io           = nullptr;

// ---------------------------------------------------------------------------
// Helper: throw a typed Python exception when a Result is not OK.
// ---------------------------------------------------------------------------
void throw_on_error(vxl::ErrorCode code, const std::string& msg) {
    if (code == vxl::ErrorCode::OK) return;

    std::string full_msg = msg.empty() ? vxl::error_code_to_string(code) : msg;

    switch (code) {
        case vxl::ErrorCode::DEVICE_NOT_FOUND:
        case vxl::ErrorCode::DEVICE_OPEN_FAILED:
        case vxl::ErrorCode::DEVICE_TIMEOUT:
        case vxl::ErrorCode::DEVICE_DISCONNECTED:
            PyErr_SetString(exc_device->ptr(), full_msg.c_str());
            throw py::error_already_set();

        case vxl::ErrorCode::CALIB_INSUFFICIENT_DATA:
        case vxl::ErrorCode::CALIB_CONVERGENCE_FAILED:
            PyErr_SetString(exc_calibration->ptr(), full_msg.c_str());
            throw py::error_already_set();

        case vxl::ErrorCode::RECONSTRUCT_LOW_MODULATION:
        case vxl::ErrorCode::RECONSTRUCT_PHASE_UNWRAP_FAILED:
            PyErr_SetString(exc_reconstruct->ptr(), full_msg.c_str());
            throw py::error_already_set();

        case vxl::ErrorCode::INSPECT_NO_REFERENCE:
        case vxl::ErrorCode::INSPECT_ROI_OUT_OF_BOUNDS:
            PyErr_SetString(exc_inspect->ptr(), full_msg.c_str());
            throw py::error_already_set();

        case vxl::ErrorCode::MODEL_LOAD_FAILED:
        case vxl::ErrorCode::MODEL_INPUT_MISMATCH:
            PyErr_SetString(exc_model->ptr(), full_msg.c_str());
            throw py::error_already_set();

        case vxl::ErrorCode::IO_CONNECTION_FAILED:
        case vxl::ErrorCode::IO_WRITE_FAILED:
        case vxl::ErrorCode::IO_READ_FAILED:
        case vxl::ErrorCode::IO_NOT_SUPPORTED:
            PyErr_SetString(exc_io->ptr(), full_msg.c_str());
            throw py::error_already_set();

        default:
            PyErr_SetString(exc_vxl->ptr(), full_msg.c_str());
            throw py::error_already_set();
    }
}

// ---------------------------------------------------------------------------
// init_error -- called from module.cpp
// ---------------------------------------------------------------------------
void init_error(py::module_& m) {
    // Enum
    py::enum_<vxl::ErrorCode>(m, "ErrorCode")
        .value("OK",                          vxl::ErrorCode::OK)
        .value("DEVICE_NOT_FOUND",            vxl::ErrorCode::DEVICE_NOT_FOUND)
        .value("DEVICE_OPEN_FAILED",          vxl::ErrorCode::DEVICE_OPEN_FAILED)
        .value("DEVICE_TIMEOUT",              vxl::ErrorCode::DEVICE_TIMEOUT)
        .value("DEVICE_DISCONNECTED",         vxl::ErrorCode::DEVICE_DISCONNECTED)
        .value("CALIB_INSUFFICIENT_DATA",     vxl::ErrorCode::CALIB_INSUFFICIENT_DATA)
        .value("CALIB_CONVERGENCE_FAILED",    vxl::ErrorCode::CALIB_CONVERGENCE_FAILED)
        .value("RECONSTRUCT_LOW_MODULATION",  vxl::ErrorCode::RECONSTRUCT_LOW_MODULATION)
        .value("RECONSTRUCT_PHASE_UNWRAP_FAILED", vxl::ErrorCode::RECONSTRUCT_PHASE_UNWRAP_FAILED)
        .value("INSPECT_NO_REFERENCE",        vxl::ErrorCode::INSPECT_NO_REFERENCE)
        .value("INSPECT_ROI_OUT_OF_BOUNDS",   vxl::ErrorCode::INSPECT_ROI_OUT_OF_BOUNDS)
        .value("MODEL_LOAD_FAILED",           vxl::ErrorCode::MODEL_LOAD_FAILED)
        .value("MODEL_INPUT_MISMATCH",        vxl::ErrorCode::MODEL_INPUT_MISMATCH)
        .value("IO_CONNECTION_FAILED",        vxl::ErrorCode::IO_CONNECTION_FAILED)
        .value("IO_WRITE_FAILED",             vxl::ErrorCode::IO_WRITE_FAILED)
        .value("IO_READ_FAILED",              vxl::ErrorCode::IO_READ_FAILED)
        .value("IO_NOT_SUPPORTED",            vxl::ErrorCode::IO_NOT_SUPPORTED)
        .value("INVALID_PARAMETER",           vxl::ErrorCode::INVALID_PARAMETER)
        .value("FILE_NOT_FOUND",              vxl::ErrorCode::FILE_NOT_FOUND)
        .value("OUT_OF_MEMORY",              vxl::ErrorCode::OUT_OF_MEMORY)
        .value("INTERNAL_ERROR",              vxl::ErrorCode::INTERNAL_ERROR)
        .export_values();

    m.def("error_code_to_string", &vxl::error_code_to_string,
          py::arg("code"),
          "Return a human-readable string for an ErrorCode value.");

    // Exception hierarchy.  VxlError is the base; the rest derive from it.
    static py::exception<std::runtime_error> vxl_error(m, "VxlError");
    exc_vxl = &vxl_error;

    static py::exception<std::runtime_error> device_error(m, "DeviceError", vxl_error.ptr());
    exc_device = &device_error;

    static py::exception<std::runtime_error> calibration_error(m, "CalibrationError", vxl_error.ptr());
    exc_calibration = &calibration_error;

    static py::exception<std::runtime_error> reconstruct_error(m, "ReconstructError", vxl_error.ptr());
    exc_reconstruct = &reconstruct_error;

    static py::exception<std::runtime_error> inspect_error(m, "InspectError", vxl_error.ptr());
    exc_inspect = &inspect_error;

    static py::exception<std::runtime_error> model_error(m, "ModelError", vxl_error.ptr());
    exc_model = &model_error;

    static py::exception<std::runtime_error> io_error(m, "IOError_", vxl_error.ptr());
    exc_io = &io_error;

    // set_error_callback
    m.def("set_error_callback",
          [](py::object cb) {
              vxl::set_error_callback(
                  [cb](vxl::ErrorCode code, const std::string& msg) {
                      py::gil_scoped_acquire gil;
                      cb(code, msg);
                  });
          },
          py::arg("callback"),
          "Register a global error callback (Python callable(ErrorCode, str)).");
}
