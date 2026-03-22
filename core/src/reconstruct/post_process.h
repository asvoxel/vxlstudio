#pragma once

// Internal module: Post-processing filters for point clouds and height maps
//
// - Statistical outlier removal on point clouds
// - Median / Gaussian filtering on height maps (using OpenCV)

#include <string>
#include "vxl/types.h"

namespace vxl::detail {

/// Remove statistical outliers from a point cloud.
/// For each point, compute the mean distance to its k nearest neighbors.
/// Points whose mean distance exceeds (global_mean + std_ratio * global_std)
/// are removed.
/// @param cloud        Input XYZ point cloud.
/// @param k_neighbors  Number of nearest neighbors to consider (default: 8).
/// @param std_ratio    Standard deviation multiplier threshold (default: 2.0).
/// @return Filtered point cloud.
PointCloud filter_outliers(
    const PointCloud& cloud,
    int k_neighbors = 8,
    float std_ratio = 2.0f);

/// Apply a spatial filter to a height map.
/// @param hmap         Input height map.
/// @param type         Filter type: "median" or "gaussian".
/// @param kernel_size  Filter kernel size (must be odd, >= 3).
/// @return Filtered height map. NaN regions are preserved.
HeightMap filter_height_map(
    const HeightMap& hmap,
    const std::string& type,
    int kernel_size);

} // namespace vxl::detail
