// VxlStudio Python bindings -- Reconstruct
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/compute.h"
#include "vxl/reconstruct.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_reconstruct(py::module_& m) {
    // ---- ReconstructParams -------------------------------------------------
    py::class_<vxl::ReconstructParams>(m, "ReconstructParams")
        .def(py::init<>())
        .def_readwrite("method",                  &vxl::ReconstructParams::method)
        .def_readwrite("phase_shift_steps",        &vxl::ReconstructParams::phase_shift_steps)
        .def_readwrite("frequencies",              &vxl::ReconstructParams::frequencies)
        .def_readwrite("height_map_resolution_mm", &vxl::ReconstructParams::height_map_resolution_mm)
        .def_readwrite("min_modulation",           &vxl::ReconstructParams::min_modulation)
        .def_readwrite("filter_type",              &vxl::ReconstructParams::filter_type)
        .def_readwrite("filter_kernel_size",       &vxl::ReconstructParams::filter_kernel_size)
        .def("__repr__", [](const vxl::ReconstructParams& p) {
            return "ReconstructParams(method=" + p.method +
                   ", steps=" + std::to_string(p.phase_shift_steps) + ")";
        });

    // ---- ReconstructOutput -------------------------------------------------
    py::class_<vxl::ReconstructOutput>(m, "ReconstructOutput")
        .def(py::init<>())
        .def_readwrite("height_map",      &vxl::ReconstructOutput::height_map)
        .def_readwrite("point_cloud",     &vxl::ReconstructOutput::point_cloud)
        .def_readwrite("modulation_mask", &vxl::ReconstructOutput::modulation_mask)
        .def("__repr__", [](const vxl::ReconstructOutput& o) {
            return "ReconstructOutput(hmap=" + std::to_string(o.height_map.width) +
                   "x" + std::to_string(o.height_map.height) +
                   ", points=" + std::to_string(o.point_cloud.point_count) + ")";
        });

    // ---- IDepthProvider base class (for Python-side extension) -------------
    class PyDepthProvider : public vxl::IDepthProvider {
    public:
        using vxl::IDepthProvider::IDepthProvider;

        std::string type() const override {
            PYBIND11_OVERRIDE_PURE(std::string, vxl::IDepthProvider, type);
        }

        vxl::Result<vxl::ReconstructOutput> process(
            const std::vector<vxl::Image>& images,
            const vxl::CalibrationParams& calib,
            const vxl::ReconstructParams& params) override
        {
            PYBIND11_OVERRIDE_PURE(
                vxl::Result<vxl::ReconstructOutput>,
                vxl::IDepthProvider, process,
                images, calib, params);
        }
    };

    py::class_<vxl::IDepthProvider, PyDepthProvider,
               std::shared_ptr<vxl::IDepthProvider>>(m, "IDepthProvider")
        .def(py::init<>())
        .def("type", &vxl::IDepthProvider::type)
        .def("process", &vxl::IDepthProvider::process,
             py::arg("images"), py::arg("calib"), py::arg("params"));

    // ---- Reconstruct class (static methods) --------------------------------
    py::class_<vxl::Reconstruct>(m, "Reconstruct")
        .def_static("from_fringe",
            [](const std::vector<vxl::Image>& frames,
               const vxl::CalibrationParams& calib,
               const vxl::ReconstructParams& params) {
                vxl::Result<vxl::ReconstructOutput> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Reconstruct::from_fringe(frames, calib, params);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("frames"), py::arg("calib"),
            py::arg("params") = vxl::ReconstructParams{},
            "Full reconstruction from fringe-pattern images. Returns ReconstructOutput.")

        .def_static("from_stereo",
            [](const vxl::Image& left, const vxl::Image& right,
               const vxl::CalibrationParams& calib,
               const vxl::ReconstructParams& params) {
                vxl::Result<vxl::ReconstructOutput> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Reconstruct::from_stereo(left, right, calib, params);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("left"), py::arg("right"), py::arg("calib"),
            py::arg("params") = vxl::ReconstructParams{},
            "Stereo matching reconstruction from left/right image pair.")

        .def_static("from_depth",
            [](const vxl::Image& depth_map,
               const vxl::CalibrationParams& calib,
               float max_depth_mm) {
                vxl::Result<vxl::ReconstructOutput> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Reconstruct::from_depth(depth_map, calib, max_depth_mm);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("depth_map"), py::arg("calib"),
            py::arg("max_depth_mm") = 5000.0f,
            "Direct depth map to point cloud conversion.")

        .def_static("process",
            [](const std::string& provider_type,
               const std::vector<vxl::Image>& images,
               const vxl::CalibrationParams& calib,
               const vxl::ReconstructParams& params) {
                vxl::Result<vxl::ReconstructOutput> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Reconstruct::process(provider_type, images, calib, params);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("provider_type"), py::arg("images"),
            py::arg("calib"),
            py::arg("params") = vxl::ReconstructParams{},
            "Generic dispatch by provider type string.");

    // ---- ComputeBackend enum -------------------------------------------------
    py::enum_<vxl::ComputeBackend>(m, "ComputeBackend")
        .value("CPU",    vxl::ComputeBackend::CPU)
        .value("CUDA",   vxl::ComputeBackend::CUDA)
        .value("METAL",  vxl::ComputeBackend::METAL)
        .value("OPENCL", vxl::ComputeBackend::OPENCL);

    // ---- Compute backend management ------------------------------------------
    m.def("set_compute_backend",
        [](vxl::ComputeBackend backend) {
            auto r = vxl::set_compute_backend(backend);
            throw_on_error(r.code, r.message);
        },
        py::arg("backend"),
        "Set the active compute backend (CPU, CUDA, METAL, OPENCL).");

    m.def("get_compute_backend",
        &vxl::get_compute_backend,
        "Return the currently active compute backend.");

    m.def("available_backends",
        &vxl::available_backends,
        "List all compute backends available on this system.");

    // ---- Free convenience function -----------------------------------------
    m.def("reconstruct",
        [](const std::vector<vxl::Image>& frames,
           const vxl::CalibrationParams& calib,
           const vxl::ReconstructParams& params) {
            vxl::Result<vxl::HeightMap> r;
            {
                py::gil_scoped_release release;
                r = vxl::reconstruct(frames, calib, params);
            }
            throw_on_error(r.code, r.message);
            return r.value;
        },
        py::arg("frames"), py::arg("calib"),
        py::arg("params") = vxl::ReconstructParams{},
        "Reconstruct a HeightMap from fringe-pattern images (convenience wrapper).");
}
