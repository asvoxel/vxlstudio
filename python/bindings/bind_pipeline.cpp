// VxlStudio Python bindings -- Pipeline
// SPDX-License-Identifier: MIT

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <spdlog/spdlog.h>

#include "vxl/pipeline.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_pipeline(py::module_& m) {
    // ---- StepType enum -----------------------------------------------------
    py::enum_<vxl::StepType>(m, "StepType")
        .value("CAPTURE",      vxl::StepType::CAPTURE)
        .value("RECONSTRUCT",  vxl::StepType::RECONSTRUCT)
        .value("INSPECT_3D",   vxl::StepType::INSPECT_3D)
        .value("INSPECT_2D",   vxl::StepType::INSPECT_2D)
        .value("OUTPUT",       vxl::StepType::OUTPUT)
        .value("CUSTOM",       vxl::StepType::CUSTOM);

    // ---- PipelineStep ------------------------------------------------------
    py::class_<vxl::PipelineStep>(m, "PipelineStep")
        .def(py::init<>())
        .def_readwrite("type",   &vxl::PipelineStep::type)
        .def_readwrite("name",   &vxl::PipelineStep::name)
        .def_readwrite("params", &vxl::PipelineStep::params)
        .def("__repr__", [](const vxl::PipelineStep& s) {
            return "PipelineStep(name=" + s.name + ")";
        });

    // ---- PipelineContext ----------------------------------------------------
    py::class_<vxl::PipelineContext>(m, "PipelineContext")
        .def(py::init<>())
        .def_readwrite("frames",      &vxl::PipelineContext::frames)
        .def_readwrite("height_map",  &vxl::PipelineContext::height_map)
        .def_readwrite("point_cloud", &vxl::PipelineContext::point_cloud)
        .def_readwrite("result",      &vxl::PipelineContext::result);

    // ---- Pipeline ----------------------------------------------------------
    py::class_<vxl::Pipeline>(m, "Pipeline")
        .def(py::init<>())
        .def_static("load",
            [](const std::string& path) {
                auto r = vxl::Pipeline::load(path);
                throw_on_error(r.code, r.message);
                return std::move(r.value);
            },
            py::arg("path"),
            "Load a pipeline definition from a JSON file.")
        .def("add_step", &vxl::Pipeline::add_step, py::arg("step"),
             "Append a step to the pipeline.")
        .def("set_custom_callback",
            [](vxl::Pipeline& self, const std::string& name, py::function cb) {
                // Wrap the Python callable; acquire GIL when invoking.
                self.set_custom_callback(name,
                    [cb = std::move(cb)](vxl::PipelineContext& ctx)
                        -> vxl::Result<void> {
                        py::gil_scoped_acquire acquire;
                        try {
                            cb(py::cast(ctx, py::return_value_policy::reference));
                            return vxl::Result<void>::success();
                        } catch (const py::error_already_set& e) {
                            return vxl::Result<void>::failure(
                                vxl::ErrorCode::INTERNAL_ERROR,
                                std::string("Python callback error: ") + e.what());
                        }
                    });
            },
            py::arg("step_name"), py::arg("callback"),
            "Register a Python callable for a CUSTOM step.")
        .def("set_camera_id",    &vxl::Pipeline::set_camera_id,
             py::arg("id"))
        .def("set_calibration",  &vxl::Pipeline::set_calibration,
             py::arg("calib"))
        .def("set_recipe",       &vxl::Pipeline::set_recipe,
             py::arg("recipe_path"))
        .def("run_once",
            [](vxl::Pipeline& self) {
                vxl::Result<vxl::PipelineContext> r;
                {
                    py::gil_scoped_release release;
                    r = self.run_once();
                }
                throw_on_error(r.code, r.message);
                return std::move(r.value);
            },
            "Execute all steps sequentially (releases GIL).")
        .def("start",
            [](vxl::Pipeline& self, py::function on_complete) {
                self.start(
                    [cb = std::move(on_complete)](const vxl::PipelineContext& ctx) {
                        py::gil_scoped_acquire acquire;
                        try {
                            cb(ctx);
                        } catch (const py::error_already_set& e) {
                            spdlog::error("Pipeline on_complete callback error: {}",
                                          e.what());
                        }
                    });
            },
            py::arg("on_complete"),
            "Start continuous execution in a background thread.")
        .def("stop", &vxl::Pipeline::stop,
             "Stop the continuous execution loop.")
        .def("is_running", &vxl::Pipeline::is_running)
        .def("steps",
             &vxl::Pipeline::steps,
             py::return_value_policy::reference_internal,
             "Return the ordered list of pipeline steps.")
        .def("save",
            [](const vxl::Pipeline& self, const std::string& path) {
                auto r = self.save(path);
                throw_on_error(r.code, r.message);
            },
            py::arg("path"),
            "Serialize the pipeline definition to a JSON file.");
}
