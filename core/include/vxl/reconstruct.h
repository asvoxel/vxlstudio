#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vxl/calibration.h"
#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// ReconstructParams -- Configuration for structured light reconstruction
// ---------------------------------------------------------------------------
struct VXL_EXPORT ReconstructParams {
    std::string method = "multi_frequency";   // "multi_frequency" or "gray_code"
    int phase_shift_steps = 4;                 // N-step PSP (typically 3 or 4)
    std::vector<int> frequencies = {1, 8, 64}; // fringe frequencies
    float height_map_resolution_mm = 0.05f;
    float min_modulation = 10.0f;              // below this = invalid
    std::string filter_type = "median";
    int filter_kernel_size = 3;
};

// ---------------------------------------------------------------------------
// ReconstructOutput -- Full output from reconstruction pipeline
// ---------------------------------------------------------------------------
struct VXL_EXPORT ReconstructOutput {
    HeightMap   height_map;
    PointCloud  point_cloud;
    Image       modulation_mask;   // quality/validity mask (GRAY8, 255=valid)
};

// ---------------------------------------------------------------------------
// IDepthProvider -- Abstract provider interface for pluggable 3D acquisition
// ---------------------------------------------------------------------------
class VXL_EXPORT IDepthProvider {
public:
    virtual ~IDepthProvider() = default;

    /// Returns the provider type string (e.g. "lidar", "tof").
    virtual std::string type() const = 0;

    /// Process input images into a 3D reconstruction output.
    virtual Result<ReconstructOutput> process(
        const std::vector<Image>& images,
        const CalibrationParams& calib,
        const ReconstructParams& params) = 0;
};

// ---------------------------------------------------------------------------
// Reconstruct -- Multi-source 3D reconstruction engine
// ---------------------------------------------------------------------------
class VXL_EXPORT Reconstruct {
public:
    /// Full reconstruction from fringe-pattern images (structured light).
    /// @param frames  All captured fringe images, ordered as:
    ///                [freq0_step0, freq0_step1, ..., freq1_step0, ...]
    /// @param calib   Camera-projector calibration parameters.
    /// @param params  Reconstruction algorithm settings.
    static Result<ReconstructOutput> from_fringe(
        const std::vector<Image>& frames,
        const CalibrationParams& calib,
        const ReconstructParams& params = {});

    /// Stereo matching reconstruction (left + right images -> disparity -> depth -> point cloud).
    /// @param left    Left camera image (GRAY8).
    /// @param right   Right camera image (GRAY8).
    /// @param calib   Stereo calibration. camera_matrix/camera_distortion = left intrinsics;
    ///                projector_matrix/projector_distortion = right intrinsics;
    ///                rotation/translation = right camera relative to left.
    /// @param params  Reconstruction settings (height_map_resolution_mm is used).
    static Result<ReconstructOutput> from_stereo(
        const Image& left, const Image& right,
        const CalibrationParams& calib,
        const ReconstructParams& params = {});

    /// Direct depth map to point cloud conversion.
    /// @param depth_map   Depth image: GRAY16 (uint16, mm) or FLOAT32 (float, mm).
    /// @param calib       Camera intrinsics (camera_matrix has fx, fy, cx, cy).
    /// @param max_depth_mm  Maximum valid depth in mm (points beyond this are skipped).
    static Result<ReconstructOutput> from_depth(
        const Image& depth_map,
        const CalibrationParams& calib,
        float max_depth_mm = 5000.0f);

    /// Generic dispatch by provider type string.
    /// Built-in types: "structured_light", "stereo", "depth_direct".
    /// Unknown types are looked up in registered custom providers.
    static Result<ReconstructOutput> process(
        const std::string& provider_type,
        const std::vector<Image>& images,
        const CalibrationParams& calib,
        const ReconstructParams& params = {});

    /// Register a custom depth provider (for plugins).
    /// The provider is keyed by provider->type().
    static void register_provider(const std::string& type,
                                  std::unique_ptr<IDepthProvider> provider);
};

// ---------------------------------------------------------------------------
// Convenience free function -- returns just the HeightMap
// ---------------------------------------------------------------------------
VXL_EXPORT Result<HeightMap> reconstruct(
    const std::vector<Image>& frames,
    const CalibrationParams& calib,
    const ReconstructParams& params = {});

} // namespace vxl
