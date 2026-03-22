#pragma once

// Internal module: Stereo matching reconstruction
//
// Takes a rectified or raw stereo image pair (left + right), computes
// disparity via StereoSGBM, reprojects to 3D, and generates a height map.

#include "vxl/calibration.h"
#include "vxl/error.h"
#include "vxl/reconstruct.h"
#include "vxl/types.h"

namespace vxl::detail {

/// Perform stereo matching reconstruction from a left/right image pair.
/// @param left    Left camera image (GRAY8).
/// @param right   Right camera image (GRAY8).
/// @param calib   Stereo calibration parameters:
///                - camera_matrix / camera_distortion = left camera intrinsics
///                - projector_matrix / projector_distortion = right camera intrinsics
///                - rotation / translation = right camera relative to left
/// @param params  Reconstruction settings (height_map_resolution_mm used for grid).
/// @return ReconstructOutput with point_cloud, height_map, and validity mask.
Result<ReconstructOutput> stereo_to_cloud(
    const Image& left,
    const Image& right,
    const CalibrationParams& calib,
    const ReconstructParams& params);

} // namespace vxl::detail
