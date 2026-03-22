#pragma once

// Internal module: Direct depth map to point cloud conversion
//
// Converts a camera-provided depth map (GRAY16 in mm or FLOAT32 in mm)
// into a 3D point cloud using the camera intrinsics, then generates
// a height map.

#include "vxl/calibration.h"
#include "vxl/error.h"
#include "vxl/reconstruct.h"
#include "vxl/types.h"

namespace vxl::detail {

/// Convert a depth map image to a ReconstructOutput (point cloud + height map).
/// @param depth_map      Input depth image: GRAY16 (uint16 mm) or FLOAT32 (float mm).
/// @param calib          Camera intrinsics (camera_matrix: fx, fy, cx, cy).
/// @param max_depth_mm   Maximum valid depth; pixels beyond this are skipped.
/// @param resolution_mm  Height map grid resolution in mm.
/// @return ReconstructOutput with point_cloud, height_map, and validity mask.
Result<ReconstructOutput> depth_to_cloud(
    const Image& depth_map,
    const CalibrationParams& calib,
    float max_depth_mm,
    float resolution_mm);

} // namespace vxl::detail
