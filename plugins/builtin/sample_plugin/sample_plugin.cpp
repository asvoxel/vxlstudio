// VxlStudio -- Sample plugin: flat-plane depth provider
// Demonstrates the C ABI plugin interface.
// SPDX-License-Identifier: MIT

#include "vxl/reconstruct.h"
#include "vxl/types.h"

#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// FlatDepthProvider -- trivial IDepthProvider that returns a flat point cloud
// at z = 100 mm for every input.
// ---------------------------------------------------------------------------
class FlatDepthProvider : public vxl::IDepthProvider {
public:
    std::string type() const override { return "flat_depth"; }

    vxl::Result<vxl::ReconstructOutput> process(
        const std::vector<vxl::Image>& images,
        const vxl::CalibrationParams& /*calib*/,
        const vxl::ReconstructParams& /*params*/) override
    {
        vxl::ReconstructOutput out;

        // Build a trivial point cloud: one point per pixel in the first image
        const float flat_z = 100.0f;  // millimetres

        if (!images.empty()) {
            const auto& img = images.front();
            const int w = img.width;
            const int h = img.height;
            const size_t num_points =
                static_cast<size_t>(w) * static_cast<size_t>(h);

            out.point_cloud.format = vxl::PointFormat::XYZ_FLOAT;
            out.point_cloud.point_count = num_points;
            out.point_cloud.buffer =
                vxl::SharedBuffer::allocate(num_points * 3 * sizeof(float));

            float* dst = reinterpret_cast<float*>(out.point_cloud.buffer.data());
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    size_t idx = (static_cast<size_t>(y) * w + x) * 3;
                    dst[idx + 0] = static_cast<float>(x);
                    dst[idx + 1] = static_cast<float>(y);
                    dst[idx + 2] = flat_z;
                }
            }
        }

        // Build a matching height map (all pixels = flat_z)
        if (!images.empty()) {
            const auto& img = images.front();
            out.height_map = vxl::HeightMap::create(img.width, img.height, 0.05f);
            float* hdata = reinterpret_cast<float*>(out.height_map.buffer.data());
            const size_t num_pixels =
                static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
            for (size_t i = 0; i < num_pixels; ++i) {
                hdata[i] = flat_z;
            }
        }

        return vxl::Result<vxl::ReconstructOutput>::success(std::move(out));
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// C ABI exports
// ---------------------------------------------------------------------------
extern "C" {

const char* vxl_plugin_name() {
    return "sample_flat_depth";
}

const char* vxl_plugin_version() {
    return "1.0.0";
}

const char* vxl_plugin_type() {
    return "depth_provider";
}

const char* vxl_plugin_author() {
    return "asVoxel";
}

const char* vxl_plugin_description() {
    return "Sample plugin: returns a flat point cloud at z=100mm for any input.";
}

void* vxl_plugin_create() {
    return static_cast<void*>(new FlatDepthProvider());
}

void vxl_plugin_destroy(void* ptr) {
    delete static_cast<FlatDepthProvider*>(ptr);
}

} // extern "C"
