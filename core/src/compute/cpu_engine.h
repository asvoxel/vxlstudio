#pragma once

// CPU compute engine -- delegates to the existing detail:: phase-shift and
// phase-unwrap implementations.

#include "vxl/compute.h"

namespace vxl {

class CpuEngine final : public IComputeEngine {
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
