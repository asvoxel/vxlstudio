#include "vxl/inspector_3d.h"

#include "vxl/height_map.h"

#include <algorithm>
#include <cmath>

namespace vxl {

Result<float> Inspector3D::flatness(const HeightMap& hmap, const ROI& roi) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<float>::failure(ErrorCode::INVALID_PARAMETER,
                                     "HeightMap is empty");
    }

    // Fit best plane in the ROI.
    auto plane_res = HeightMapProcessor::fit_reference_plane(hmap, roi);
    if (!plane_res.ok()) {
        return Result<float>::failure(plane_res.code, plane_res.message);
    }

    const Plane& plane = plane_res.value;

    // Clamp ROI.
    int rx = std::max(0, roi.x);
    int ry = std::max(0, roi.y);
    int rw = std::min(roi.x + roi.w, hmap.width)  - rx;
    int rh = std::min(roi.y + roi.h, hmap.height) - ry;

    const float* data = reinterpret_cast<const float*>(hmap.buffer.data());

    double max_dev = 0.0;

    for (int y = ry; y < ry + rh; ++y) {
        for (int x = rx; x < rx + rw; ++x) {
            float z = data[y * hmap.width + x];
            if (std::isnan(z)) continue;
            double dev = std::abs(plane.distance(
                static_cast<double>(x), static_cast<double>(y),
                static_cast<double>(z)));
            if (dev > max_dev) max_dev = dev;
        }
    }

    return Result<float>::success(static_cast<float>(max_dev));
}

} // namespace vxl
