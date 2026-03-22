// Internal module: Point cloud to height map conversion
//
// Algorithm:
//   1. Compute bounding box of the point cloud (or use provided ROI).
//   2. Create a regular grid at the requested resolution.
//   3. For each point, find the grid cell and update with nearest-neighbor
//      (keeping the closest Z value to the sensor, or averaging if desired).
//   4. Mark empty cells as NaN.

#include "height_map_gen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace vxl::detail {

HeightMap cloud_to_height_map(
    const PointCloud& cloud,
    float resolution_mm,
    const ROI& roi)
{
    if (resolution_mm <= 0.0f) {
        return {};
    }

    if (cloud.point_count == 0 || cloud.buffer.data() == nullptr) {
        // Return a valid but empty HeightMap instead of undefined behavior
        HeightMap hmap = HeightMap::create(0, 0, resolution_mm);
        return hmap;
    }

    const float* pts = reinterpret_cast<const float*>(cloud.buffer.data());
    const size_t n = cloud.point_count;

    // Determine bounding box
    float x_min, x_max, y_min, y_max;

    if (roi.area() > 0) {
        // Use provided ROI (in mm coordinates)
        x_min = static_cast<float>(roi.x);
        y_min = static_cast<float>(roi.y);
        x_max = static_cast<float>(roi.x + roi.w);
        y_max = static_cast<float>(roi.y + roi.h);
    } else {
        // Auto-compute from point cloud
        x_min = std::numeric_limits<float>::max();
        x_max = std::numeric_limits<float>::lowest();
        y_min = std::numeric_limits<float>::max();
        y_max = std::numeric_limits<float>::lowest();

        for (size_t i = 0; i < n; ++i) {
            float x = pts[i * 3 + 0];
            float y = pts[i * 3 + 1];
            if (std::isnan(x) || std::isnan(y)) continue;
            x_min = std::min(x_min, x);
            x_max = std::max(x_max, x);
            y_min = std::min(y_min, y);
            y_max = std::max(y_max, y);
        }

        if (x_min > x_max) {
            return {}; // all NaN
        }
    }

    // Grid dimensions -- ensure at least 2x2 so that points whose X/Y range
    // is smaller than resolution_mm still produce a valid grid.
    int grid_w = static_cast<int>(std::ceil((x_max - x_min) / resolution_mm)) + 1;
    int grid_h = static_cast<int>(std::ceil((y_max - y_min) / resolution_mm)) + 1;

    grid_w = std::max(grid_w, 2);
    grid_h = std::max(grid_h, 2);

    // Clamp to reasonable size to avoid OOM
    const int max_dim = 16384;
    grid_w = std::min(grid_w, max_dim);
    grid_h = std::min(grid_h, max_dim);

    // Allocate accumulation buffers
    // Use sum+count for averaging heights in each cell
    std::vector<double> z_sum(grid_w * grid_h, 0.0);
    std::vector<int> z_count(grid_w * grid_h, 0);

    // Bin points into grid cells
    for (size_t i = 0; i < n; ++i) {
        float x = pts[i * 3 + 0];
        float y = pts[i * 3 + 1];
        float z = pts[i * 3 + 2];

        if (std::isnan(x) || std::isnan(y) || std::isnan(z)) continue;

        int gx = static_cast<int>((x - x_min) / resolution_mm);
        int gy = static_cast<int>((y - y_min) / resolution_mm);

        // Clamp to valid grid range -- points exactly at x_max/y_max
        // would otherwise produce gx == grid_w (out of bounds).
        gx = std::clamp(gx, 0, grid_w - 1);
        gy = std::clamp(gy, 0, grid_h - 1);

        int idx = gy * grid_w + gx;
        z_sum[idx] += z;
        z_count[idx]++;
    }

    // Create HeightMap
    HeightMap hmap = HeightMap::create(grid_w, grid_h, resolution_mm);
    hmap.origin_x = x_min;
    hmap.origin_y = y_min;

    float* hdata = reinterpret_cast<float*>(hmap.buffer.data());
    const float nan_val = std::numeric_limits<float>::quiet_NaN();

    for (int i = 0; i < grid_w * grid_h; ++i) {
        if (z_count[i] > 0) {
            hdata[i] = static_cast<float>(z_sum[i] / z_count[i]);
        } else {
            hdata[i] = nan_val;
        }
    }

    return hmap;
}

} // namespace vxl::detail
