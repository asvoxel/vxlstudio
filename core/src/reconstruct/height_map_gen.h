#pragma once

// Internal module: Point cloud to height map conversion
//
// Projects 3D points onto the XY plane and creates a regular grid at the
// specified resolution. Heights are interpolated; holes (no data) are NaN.

#include "vxl/types.h"

namespace vxl::detail {

/// Convert a point cloud to a regular-grid height map.
/// @param cloud          Input XYZ point cloud.
/// @param resolution_mm  Grid cell size in mm.
/// @param roi            Optional region of interest. If area()==0, auto-compute
///                       bounding box from the point cloud.
/// @return HeightMap with resolution and origin set. Empty cells = NaN.
HeightMap cloud_to_height_map(
    const PointCloud& cloud,
    float resolution_mm,
    const ROI& roi = {});

} // namespace vxl::detail
