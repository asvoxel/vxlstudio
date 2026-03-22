#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vxl/calibration.h"
#include "vxl/camera.h"
#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// CameraManager -- manage multiple 3D cameras with calibration data
// ---------------------------------------------------------------------------
class VXL_EXPORT CameraManager {
public:
    CameraManager();
    ~CameraManager();

    /// Add a camera by device id, together with its calibration parameters.
    Result<void> add_camera(const std::string& device_id,
                            const CalibrationParams& calib);

    /// Remove a camera by device id.
    Result<void> remove_camera(const std::string& device_id);

    /// List all managed camera device ids.
    std::vector<std::string> camera_ids() const;

    /// Get a camera pointer by device id (nullptr if not found).
    ICamera3D* get_camera(const std::string& device_id);

    /// Get calibration parameters for a camera (throws INVALID_PARAMETER
    /// via Result if not found).
    const CalibrationParams& get_calibration(const std::string& device_id) const;

    /// Capture from all cameras (sequential). Returns a map of device_id ->
    /// captured frame sequences.
    Result<std::unordered_map<std::string, std::vector<Image>>> capture_all();

    /// Aggregate per-camera inspection results into a single result.
    /// Overall OK only if every camera is OK; defects are merged.
    static InspectionResult aggregate_results(
        const std::vector<std::pair<std::string, InspectionResult>>& per_camera_results);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
