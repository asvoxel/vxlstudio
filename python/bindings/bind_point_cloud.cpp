// VxlStudio Python bindings -- PointCloudOps
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/point_cloud.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_point_cloud(py::module_& m) {
    py::class_<vxl::PointCloudOps>(m, "PointCloudOps")
        .def_static("filter_statistical",
            [](const vxl::PointCloud& cloud, int k, double std_ratio) {
                vxl::Result<vxl::PointCloud> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::PointCloudOps::filter_statistical(cloud, k, std_ratio);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("cloud"),
            py::arg("k_neighbors") = 20,
            py::arg("std_ratio") = 2.0,
            "Statistical outlier removal. Removes points whose mean neighbor "
            "distance exceeds (global_mean + std_ratio * global_std).")
        .def_static("downsample_voxel",
            [](const vxl::PointCloud& cloud, double voxel_size) {
                vxl::Result<vxl::PointCloud> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::PointCloudOps::downsample_voxel(cloud, voxel_size);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("cloud"),
            py::arg("voxel_size"),
            "Voxel grid downsampling. Averages points within each voxel cell.");
}
