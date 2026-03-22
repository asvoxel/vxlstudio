#pragma once

// Metal compute engine -- stub implementation.
//
// When VXL_HAS_METAL is defined at compile time the real Metal pipeline will
// be compiled (requires Objective-C++ and .metal shader files); otherwise
// every method returns an "unavailable" error.

#include "vxl/compute.h"

#ifdef VXL_HAS_METAL

namespace vxl {

class MetalEngine final : public IComputeEngine {
public:
    ComputeBackend type() const override;
    std::string name() const override;
    bool is_available() const override;

    Result<std::pair<cv::Mat, cv::Mat>> compute_phase(
        const std::vector<cv::Mat>& frames, int steps) override;

    Result<cv::Mat> unwrap_phase(
        const std::vector<cv::Mat>& wrapped_phases,
        const std::vector<int>& frequencies,
        const std::vector<cv::Mat>& modulations,
        float min_modulation) override;
};

} // namespace vxl

#endif // VXL_HAS_METAL
