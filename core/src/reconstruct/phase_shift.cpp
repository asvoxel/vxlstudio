// Internal module: N-step phase shifting profilometry
//
// Implements the standard N-step PSP algorithm. For N equally-spaced phase
// shifts at delta_k = 2*pi*k/N, the phase and modulation are:
//
//   numerator   = -sum_k( I_k * sin(delta_k) )
//   denominator =  sum_k( I_k * cos(delta_k) )
//   phase       = atan2(numerator, denominator)
//   modulation  = (2/N) * sqrt(numerator^2 + denominator^2)
//
// This formulation works for any N >= 3 and naturally rejects DC bias.

#include "phase_shift.h"

#include <cmath>
#include <stdexcept>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl::detail {

PhaseResult compute_phase(const std::vector<cv::Mat>& frames, int steps)
{
    if (steps < 3) {
        throw std::invalid_argument("Phase shift requires at least 3 steps");
    }
    if (static_cast<int>(frames.size()) != steps) {
        throw std::invalid_argument(
            "Frame count (" + std::to_string(frames.size()) +
            ") != steps (" + std::to_string(steps) + ")");
    }

    const int rows = frames[0].rows;
    const int cols = frames[0].cols;

    // Convert all frames to CV_32F if needed
    std::vector<cv::Mat> fframes(steps);
    for (int k = 0; k < steps; ++k) {
        if (frames[k].rows != rows || frames[k].cols != cols) {
            throw std::invalid_argument("All frames must have the same size");
        }
        if (frames[k].type() == CV_32F) {
            fframes[k] = frames[k];
        } else {
            frames[k].convertTo(fframes[k], CV_32F);
        }
    }

    // Precompute sin/cos coefficients for each step
    const double two_pi = 2.0 * CV_PI;
    std::vector<float> sin_coeff(steps);
    std::vector<float> cos_coeff(steps);
    for (int k = 0; k < steps; ++k) {
        double delta = two_pi * k / steps;
        sin_coeff[k] = static_cast<float>(std::sin(delta));
        cos_coeff[k] = static_cast<float>(std::cos(delta));
    }

    // Accumulate numerator (sin) and denominator (cos) sums
    cv::Mat sum_sin = cv::Mat::zeros(rows, cols, CV_32F);
    cv::Mat sum_cos = cv::Mat::zeros(rows, cols, CV_32F);

    for (int k = 0; k < steps; ++k) {
        // sum_sin += I_k * sin(delta_k)
        // sum_cos += I_k * cos(delta_k)
        cv::Mat weighted;

        weighted = fframes[k] * sin_coeff[k];
        sum_sin += weighted;

        weighted = fframes[k] * cos_coeff[k];
        sum_cos += weighted;
    }

    // phase = atan2(-sum_sin, sum_cos)
    // The negation of sum_sin matches the standard PSP convention so that
    // increasing fringe order corresponds to increasing phase.
    PhaseResult result;
    result.phase = cv::Mat(rows, cols, CV_32F);
    result.modulation = cv::Mat(rows, cols, CV_32F);

    const float mod_scale = 2.0f / steps;

    for (int r = 0; r < rows; ++r) {
        const float* ss = sum_sin.ptr<float>(r);
        const float* sc = sum_cos.ptr<float>(r);
        float* ph = result.phase.ptr<float>(r);
        float* mo = result.modulation.ptr<float>(r);
        for (int c = 0; c < cols; ++c) {
            ph[c] = std::atan2(-ss[c], sc[c]);
            mo[c] = mod_scale * std::sqrt(ss[c] * ss[c] + sc[c] * sc[c]);
        }
    }

    return result;
}

} // namespace vxl::detail
