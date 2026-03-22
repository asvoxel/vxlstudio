// Internal module: Multi-frequency temporal phase unwrapping
//
// Temporal unwrapping is robust against spatial discontinuities because each
// pixel is unwrapped independently using information from multiple frequencies.
// The lowest frequency (typically 1 period across the image) is already
// unambiguous and serves as the starting reference.

#include "phase_unwrap.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace vxl::detail {

cv::Mat unwrap_temporal(
    const std::vector<cv::Mat>& wrapped_phases,
    const std::vector<int>& frequencies,
    const std::vector<cv::Mat>& modulations,
    float min_modulation)
{
    const int n_freq = static_cast<int>(wrapped_phases.size());
    if (n_freq < 1) {
        throw std::invalid_argument("At least one frequency level is required");
    }
    if (static_cast<int>(frequencies.size()) != n_freq ||
        static_cast<int>(modulations.size()) != n_freq) {
        throw std::invalid_argument(
            "wrapped_phases, frequencies, and modulations must have the same size");
    }

    const int rows = wrapped_phases[0].rows;
    const int cols = wrapped_phases[0].cols;
    const float two_pi = static_cast<float>(2.0 * CV_PI);
    const float nan_val = std::numeric_limits<float>::quiet_NaN();

    // Start with the lowest frequency -- its wrapped phase IS the unwrapped
    // phase (assuming frequency=1 means exactly one fringe period, so the
    // wrapped phase covers [-pi, pi] unambiguously).
    cv::Mat unwrapped = wrapped_phases[0].clone();

    // Mask out low-modulation pixels at the base frequency
    for (int r = 0; r < rows; ++r) {
        const float* mod = modulations[0].ptr<float>(r);
        float* uw = unwrapped.ptr<float>(r);
        for (int c = 0; c < cols; ++c) {
            if (mod[c] < min_modulation) {
                uw[c] = nan_val;
            }
        }
    }

    // Progressively unwrap higher frequencies using the previous result
    for (int i = 1; i < n_freq; ++i) {
        const float freq_ratio =
            static_cast<float>(frequencies[i]) / static_cast<float>(frequencies[i - 1]);

        cv::Mat next_unwrapped(rows, cols, CV_32F, nan_val);

        for (int r = 0; r < rows; ++r) {
            const float* uw_low  = unwrapped.ptr<float>(r);
            const float* wp_high = wrapped_phases[i].ptr<float>(r);
            const float* mod_high = modulations[i].ptr<float>(r);
            float* uw_high = next_unwrapped.ptr<float>(r);

            for (int c = 0; c < cols; ++c) {
                // Skip if lower frequency was invalid
                if (std::isnan(uw_low[c])) continue;

                // Skip if this frequency has low modulation
                if (mod_high[c] < min_modulation) continue;

                // Scale the lower-frequency unwrapped phase to the current
                // frequency's range, then find the integer fringe order k
                // such that: unwrapped_high = wrapped_high + 2*pi*k
                float predicted = uw_low[c] * freq_ratio;
                float diff = predicted - wp_high[c];
                float k = std::round(diff / two_pi);
                uw_high[c] = wp_high[c] + two_pi * k;
            }
        }

        unwrapped = next_unwrapped;
    }

    return unwrapped;
}

} // namespace vxl::detail
