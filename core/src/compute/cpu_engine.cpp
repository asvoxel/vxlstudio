// CPU compute engine -- wraps the existing detail:: functions so they
// conform to the IComputeEngine interface.

#include "cpu_engine.h"

#include "../reconstruct/phase_shift.h"
#include "../reconstruct/phase_unwrap.h"

#include <stdexcept>

namespace vxl {

ComputeBackend CpuEngine::type() const { return ComputeBackend::CPU; }

std::string CpuEngine::name() const { return "CPU"; }

bool CpuEngine::is_available() const { return true; }

Result<std::pair<cv::Mat, cv::Mat>> CpuEngine::compute_phase(
    const std::vector<cv::Mat>& frames, int steps)
{
    try {
        auto pr = detail::compute_phase(frames, steps);
        return Result<std::pair<cv::Mat, cv::Mat>>::success(
            {std::move(pr.phase), std::move(pr.modulation)});
    } catch (const std::exception& e) {
        return Result<std::pair<cv::Mat, cv::Mat>>::failure(
            ErrorCode::INVALID_PARAMETER, e.what());
    }
}

Result<cv::Mat> CpuEngine::unwrap_phase(
    const std::vector<cv::Mat>& wrapped_phases,
    const std::vector<int>& frequencies,
    const std::vector<cv::Mat>& modulations,
    float min_modulation)
{
    try {
        cv::Mat result = detail::unwrap_temporal(
            wrapped_phases, frequencies, modulations, min_modulation);
        return Result<cv::Mat>::success(std::move(result));
    } catch (const std::exception& e) {
        return Result<cv::Mat>::failure(
            ErrorCode::RECONSTRUCT_PHASE_UNWRAP_FAILED, e.what());
    }
}

} // namespace vxl
