// VxlStudio Python bindings -- VLM (Vision Language Model) assistant
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "vxl/vlm.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_vlm(py::module_& m) {
    // ---- VLMConfig ---------------------------------------------------------
    py::class_<vxl::VLMConfig>(m, "VLMConfig")
        .def(py::init<>())
        .def_readwrite("provider",        &vxl::VLMConfig::provider)
        .def_readwrite("api_key",         &vxl::VLMConfig::api_key)
        .def_readwrite("model",           &vxl::VLMConfig::model)
        .def_readwrite("base_url",        &vxl::VLMConfig::base_url)
        .def_readwrite("timeout_seconds", &vxl::VLMConfig::timeout_seconds)
        .def_readwrite("max_tokens",      &vxl::VLMConfig::max_tokens)
        .def("__repr__", [](const vxl::VLMConfig& c) {
            return "VLMConfig(provider='" + c.provider +
                   "', model='" + c.model + "')";
        });

    // ---- VLMResponse -------------------------------------------------------
    py::class_<vxl::VLMResponse>(m, "VLMResponse")
        .def(py::init<>())
        .def_readwrite("text",        &vxl::VLMResponse::text)
        .def_readwrite("confidence",  &vxl::VLMResponse::confidence)
        .def_readwrite("tokens_used", &vxl::VLMResponse::tokens_used)
        .def_readwrite("latency_ms",  &vxl::VLMResponse::latency_ms)
        .def("__repr__", [](const vxl::VLMResponse& r) {
            return "VLMResponse(tokens=" + std::to_string(r.tokens_used) +
                   ", latency=" + std::to_string(r.latency_ms) + "ms)";
        });

    // ---- VLMAssistant ------------------------------------------------------
    py::class_<vxl::VLMAssistant>(m, "VLMAssistant")
        .def(py::init<>())
        .def("configure",
            [](vxl::VLMAssistant& self, const vxl::VLMConfig& config) {
                auto r = self.configure(config);
                throw_on_error(r.code, r.message);
            },
            py::arg("config"),
            "Configure the VLM assistant with provider settings.")
        .def("is_configured", &vxl::VLMAssistant::is_configured,
             "Check if the assistant has been configured.")
        .def("config", &vxl::VLMAssistant::config,
             py::return_value_policy::reference_internal,
             "Get the current configuration.")
        .def("set_http_caller",
            [](vxl::VLMAssistant& self, py::function caller) {
                self.set_http_caller(
                    [caller](const std::string& url,
                             const std::string& headers_json,
                             const std::string& body) -> std::string {
                        py::gil_scoped_acquire gil;
                        py::object result = caller(url, headers_json, body);
                        return result.cast<std::string>();
                    });
            },
            py::arg("caller"),
            "Set a Python callable(url, headers_json, body) -> response_str as HTTP transport.")
        .def("describe_defect",
            [](vxl::VLMAssistant& self, const vxl::Image& image,
               const vxl::DefectRegion& defect) {
                vxl::Result<vxl::VLMResponse> r;
                {
                    py::gil_scoped_release release;
                    r = self.describe_defect(image, defect);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("defect"),
            "Describe a defect from an image and defect region.")
        .def("suggest_parameters",
            [](vxl::VLMAssistant& self, const vxl::Image& image,
               const std::string& context) {
                vxl::Result<vxl::VLMResponse> r;
                {
                    py::gil_scoped_release release;
                    r = self.suggest_parameters(image, context);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("sample_image"), py::arg("context") = "",
            "Suggest inspection parameters from a sample image.")
        .def("generate_report",
            [](vxl::VLMAssistant& self,
               const std::vector<vxl::InspectionResult>& results,
               const std::string& template_text) {
                vxl::Result<vxl::VLMResponse> r;
                {
                    py::gil_scoped_release release;
                    r = self.generate_report(results, template_text);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("results"), py::arg("template_text") = "",
            "Generate a quality report from inspection results.")
        .def("query",
            [](vxl::VLMAssistant& self, const std::string& prompt,
               const vxl::Image* image) {
                vxl::Result<vxl::VLMResponse> r;
                {
                    py::gil_scoped_release release;
                    r = self.query(prompt, image);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("prompt"), py::arg("image") = nullptr,
            "Free-form query with optional image.")
        .def("suggest_labels",
            [](vxl::VLMAssistant& self, const vxl::Image& image,
               const std::vector<std::string>& labels) {
                vxl::Result<std::vector<std::pair<std::string, float>>> r;
                {
                    py::gil_scoped_release release;
                    r = self.suggest_labels(image, labels);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("candidate_labels"),
            "Suggest labels for an image from a list of candidates.");

    // ---- Global singleton --------------------------------------------------
    m.def("vlm_assistant", &vxl::vlm_assistant,
          py::return_value_policy::reference,
          "Get the global VLM assistant singleton.");
}
