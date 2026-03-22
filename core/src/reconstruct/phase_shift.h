#pragma once

// Internal module: N-step phase shifting profilometry (PSP)
//
// For N equally-spaced phase shifts delta_k = 2*pi*k/N (k = 0 .. N-1):
//   phase(x,y) = atan2( -sum_k(I_k * sin(delta_k)),
//                         sum_k(I_k * cos(delta_k)) )
//   modulation(x,y) = (2/N) * sqrt(sum_sin^2 + sum_cos^2)
//
// The wrapped phase lies in [-pi, pi].

#include <vector>
#include <opencv2/core.hpp>

namespace vxl::detail {

struct PhaseResult {
    cv::Mat phase;       // CV_32F, wrapped phase in [-pi, pi]
    cv::Mat modulation;  // CV_32F, modulation amplitude (signal quality)
};

/// Compute wrapped phase and modulation from N phase-shifted images.
/// @param frames  Vector of N grayscale images (CV_8U or CV_32F).
/// @param steps   Number of phase shift steps (must equal frames.size()).
/// @return PhaseResult with wrapped phase and modulation maps.
PhaseResult compute_phase(const std::vector<cv::Mat>& frames, int steps);

} // namespace vxl::detail
