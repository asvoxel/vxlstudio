#pragma once

// Internal module: Phase-to-3D coordinate conversion
//
// Converts the unwrapped phase map to 3D coordinates using the camera-projector
// stereo calibration. For each valid pixel (u,v) with known projector column p:
//   1. Compute the camera ray through (u,v).
//   2. Compute the projector ray/plane at column p.
//   3. Triangulate to find the 3D intersection point.

#include "vxl/calibration.h"
#include "vxl/types.h"

#include <opencv2/core.hpp>

namespace vxl::detail {

/// Convert unwrapped phase map to a 3D point cloud via triangulation.
/// @param unwrapped_phase  CV_32F unwrapped phase. NaN = invalid pixel.
/// @param calib            Camera-projector calibration parameters.
/// @param validity_mask    Optional CV_8U mask (255 = valid). If empty, uses
///                         non-NaN pixels from the phase map.
/// @return PointCloud in XYZ_FLOAT format.
PointCloud phase_to_3d(
    const cv::Mat& unwrapped_phase,
    const CalibrationParams& calib,
    const cv::Mat& validity_mask = cv::Mat());

} // namespace vxl::detail
