// CUDA compute engine -- stub (returns errors when CUDA is not compiled in).
//
// To enable CUDA acceleration:
// 1. Install NVIDIA CUDA Toolkit (>= 11.0)
// 2. Compile the .cu kernels in core/src/compute/kernels/:
//      nvcc -c kernels/phase_shift.cu -o phase_shift.o
//      nvcc -c kernels/phase_unwrap.cu -o phase_unwrap.o
// 3. Link the .o files into libvxl_core
// 4. Define VXL_HAS_CUDA in CMakeLists.txt
// 5. Replace the stub bodies below with kernel launch calls

#ifdef VXL_HAS_CUDA

#include "cuda_engine.h"

// TODO: Uncomment when kernels are compiled:
// extern "C" void launch_phase_shift_kernel(
//     const unsigned char** d_frames, float* d_phase, float* d_modulation,
//     int width, int height, int num_steps);
//
// extern "C" void launch_phase_unwrap(
//     const float** d_wrapped_phases, const float** d_modulations,
//     const int* frequencies, float* d_unwrapped_out, float* d_temp_buffer,
//     int num_levels, int width, int height, float min_modulation);

namespace vxl {

ComputeBackend CudaEngine::type() const { return ComputeBackend::CUDA; }

std::string CudaEngine::name() const { return "CUDA"; }

// TODO: Probe the CUDA runtime (e.g. cudaGetDeviceCount) and return true
//       only when a usable GPU is present.
bool CudaEngine::is_available() const { return false; }

Result<std::pair<cv::Mat, cv::Mat>> CudaEngine::compute_phase(
    const std::vector<cv::Mat>& /*frames*/, int /*steps*/)
{
    // TODO: When kernels are compiled, implement as:
    //   1. Upload each frame to device memory (cudaMalloc + cudaMemcpy)
    //   2. Allocate device buffers for phase_out and modulation_out
    //   3. Call launch_phase_shift_kernel(d_frames, d_phase, d_mod, w, h, steps)
    //   4. Download results to cv::Mat (CV_32F)
    //   5. Free device memory
    return Result<std::pair<cv::Mat, cv::Mat>>::failure(
        ErrorCode::INTERNAL_ERROR,
        "CUDA compute_phase not yet implemented. "
        "See core/src/compute/kernels/phase_shift.cu");
}

Result<cv::Mat> CudaEngine::unwrap_phase(
    const std::vector<cv::Mat>& /*wrapped_phases*/,
    const std::vector<int>& /*frequencies*/,
    const std::vector<cv::Mat>& /*modulations*/,
    float /*min_modulation*/)
{
    // TODO: When kernels are compiled, implement as:
    //   1. Upload wrapped phase maps and modulation maps to device
    //   2. Allocate output buffer and temp buffer on device
    //   3. Call launch_phase_unwrap(d_phases, d_mods, freqs, d_out, d_tmp, ...)
    //   4. Download result to cv::Mat (CV_32F)
    //   5. Free device memory
    return Result<cv::Mat>::failure(
        ErrorCode::INTERNAL_ERROR,
        "CUDA unwrap_phase not yet implemented. "
        "See core/src/compute/kernels/phase_unwrap.cu");
}

} // namespace vxl

#endif // VXL_HAS_CUDA
