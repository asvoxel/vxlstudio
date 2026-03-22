#pragma once

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// PointCloudOps -- static utilities for point cloud processing
// ---------------------------------------------------------------------------
class VXL_EXPORT PointCloudOps {
public:
    /// Statistical outlier removal.
    /// For each point, compute the mean distance to its k nearest neighbors.
    /// Remove points whose mean distance exceeds (global_mean + std_ratio * global_std).
    static Result<PointCloud> filter_statistical(const PointCloud& cloud,
                                                  int k_neighbors = 20,
                                                  double std_ratio = 2.0);

    /// Voxel grid downsampling.
    /// Hash points into a voxel grid of the given size and average the
    /// positions within each occupied voxel.
    static Result<PointCloud> downsample_voxel(const PointCloud& cloud,
                                               double voxel_size);
};

} // namespace vxl
