// Metal compute engine -- stub (returns errors when Metal is not compiled in).
//
// TODO: Replace the stub bodies with real Metal compute pipelines once
//       .metal shader files and the Objective-C++ bridge are integrated.

#ifdef VXL_HAS_METAL

#include "metal_engine.h"

namespace vxl {

ComputeBackend MetalEngine::type() const { return ComputeBackend::METAL; }

std::string MetalEngine::name() const { return "Metal"; }

// TODO: Query MTLCreateSystemDefaultDevice() and return true when a
//       Metal-capable GPU is present.
bool MetalEngine::is_available() const { return false; }

Result<std::pair<cv::Mat, cv::Mat>> MetalEngine::compute_phase(
    const std::vector<cv::Mat>& /*frames*/, int /*steps*/)
{
    return Result<std::pair<cv::Mat, cv::Mat>>::failure(
        ErrorCode::INTERNAL_ERROR,
        "Metal compute_phase not yet implemented");
}

Result<cv::Mat> MetalEngine::unwrap_phase(
    const std::vector<cv::Mat>& /*wrapped_phases*/,
    const std::vector<int>& /*frequencies*/,
    const std::vector<cv::Mat>& /*modulations*/,
    float /*min_modulation*/)
{
    return Result<cv::Mat>::failure(
        ErrorCode::INTERNAL_ERROR,
        "Metal unwrap_phase not yet implemented");
}

} // namespace vxl

#endif // VXL_HAS_METAL
