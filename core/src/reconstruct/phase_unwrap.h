#pragma once

// Internal module: Multi-frequency temporal phase unwrapping
//
// Given wrapped phase maps at multiple frequencies (low to high), use the
// lower-frequency unwrapped phase to guide unwrapping of higher frequencies.
//
// Algorithm (for each pixel):
//   unwrapped_high = wrapped_high
//       + 2*pi * round( (unwrapped_low * f_high/f_low - wrapped_high) / (2*pi) )
//
// Pixels with modulation below the threshold in any frequency are masked out.

#include <vector>
#include <opencv2/core.hpp>

namespace vxl::detail {

/// Perform multi-frequency temporal phase unwrapping.
/// @param wrapped_phases  Wrapped phase maps, one per frequency (lowest first).
/// @param frequencies     Fringe frequency for each level (e.g., {1, 8, 64}).
/// @param modulations     Modulation maps, one per frequency.
/// @param min_modulation  Minimum acceptable modulation; below = invalid (NaN).
/// @return Unwrapped phase map (CV_32F). Invalid pixels are set to NaN.
cv::Mat unwrap_temporal(
    const std::vector<cv::Mat>& wrapped_phases,
    const std::vector<int>& frequencies,
    const std::vector<cv::Mat>& modulations,
    float min_modulation);

} // namespace vxl::detail
