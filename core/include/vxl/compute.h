#pragma once

// GPU/compute abstraction layer for VxlStudio
//
// Provides a backend-agnostic interface for compute-intensive operations
// (phase shifting, phase unwrapping) that can be accelerated on GPU.
// The default backend is CPU; CUDA and Metal are compile-time optional stubs.

#include "vxl/export.h"
#include "vxl/error.h"

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace vxl {

/// Supported compute backends.
enum class ComputeBackend { CPU, CUDA, METAL, OPENCL };

/// Abstract interface for compute-intensive reconstruction operations.
class VXL_EXPORT IComputeEngine {
public:
    virtual ~IComputeEngine() = default;

    /// Backend identifier.
    virtual ComputeBackend type() const = 0;

    /// Human-readable name (e.g. "CPU", "CUDA 12.x").
    virtual std::string name() const = 0;

    /// Whether this backend is usable on the current system.
    virtual bool is_available() const = 0;

    /// N-step phase shift computation.
    /// @param frames  N grayscale images (CV_8U or CV_32F).
    /// @param steps   Number of phase-shift steps (== frames.size()).
    /// @return (wrapped_phase [CV_32F], modulation [CV_32F]).
    virtual Result<std::pair<cv::Mat, cv::Mat>> compute_phase(
        const std::vector<cv::Mat>& frames, int steps) = 0;

    /// Multi-frequency temporal phase unwrapping.
    /// @param wrapped_phases  One wrapped-phase map per frequency (lowest first).
    /// @param frequencies     Fringe frequency for each level (e.g. {1, 8, 64}).
    /// @param modulations     Modulation map per frequency.
    /// @param min_modulation  Pixels below this threshold are set to NaN.
    /// @return Unwrapped phase map (CV_32F). Invalid pixels are NaN.
    virtual Result<cv::Mat> unwrap_phase(
        const std::vector<cv::Mat>& wrapped_phases,
        const std::vector<int>& frequencies,
        const std::vector<cv::Mat>& modulations,
        float min_modulation) = 0;
};

// ---------------------------------------------------------------------------
// Global compute-backend management
// ---------------------------------------------------------------------------

/// Set the active compute backend. Fails if the backend is not available.
VXL_EXPORT Result<void> set_compute_backend(ComputeBackend backend);

/// Return the currently active backend.
VXL_EXPORT ComputeBackend get_compute_backend();

/// List all backends that report is_available() == true.
VXL_EXPORT std::vector<ComputeBackend> available_backends();

/// Return a reference to the current compute engine.
VXL_EXPORT IComputeEngine& compute_engine();

} // namespace vxl
