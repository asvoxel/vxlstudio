// VxlStudio Python bindings -- Inference, InferenceParams
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/inference.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_inference(py::module_& m) {
    // ---- InferenceParams ---------------------------------------------------
    py::class_<vxl::InferenceParams>(m, "InferenceParams")
        .def(py::init<>())
        .def_readwrite("model_path", &vxl::InferenceParams::model_path)
        .def_readwrite("device",     &vxl::InferenceParams::device)
        .def_readwrite("threads",    &vxl::InferenceParams::threads)
        .def("__repr__", [](const vxl::InferenceParams& p) {
            return "InferenceParams(device=" + p.device +
                   ", threads=" + std::to_string(p.threads) + ")";
        });

    // ---- Inference ---------------------------------------------------------
    py::class_<vxl::Inference>(m, "Inference")
        .def_static("load",
            [](const std::string& onnx_path, const vxl::InferenceParams& params) {
                vxl::Result<vxl::Inference> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Inference::load(onnx_path, params);
                }
                throw_on_error(r.code, r.message);
                return std::move(r.value);
            },
            py::arg("onnx_path"),
            py::arg("params") = vxl::InferenceParams{},
            "Load an ONNX model.")
        .def("run",
            [](const vxl::Inference& self, const vxl::Image& input) {
                vxl::Result<std::vector<float>> r;
                {
                    py::gil_scoped_release release;
                    r = self.run(input);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("input"),
            "Run inference on an image. Returns flattened float tensor.")
        .def("model_path",   &vxl::Inference::model_path,
             "Get the model file path.")
        .def("input_shape",  &vxl::Inference::input_shape,
             "Get expected input shape.")
        .def("output_shape", &vxl::Inference::output_shape,
             "Get expected output shape.");
}
