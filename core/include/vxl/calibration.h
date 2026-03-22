#pragma once

#include <memory>
#include <string>

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// CalibrationParams -- camera + projector intrinsics and stereo extrinsics
// ---------------------------------------------------------------------------
struct VXL_EXPORT CalibrationParams {
    // Camera intrinsics
    double camera_matrix[9]      = {};   // 3x3 row-major
    double camera_distortion[5]  = {};   // k1, k2, p1, p2, k3
    int    image_width           = 0;
    int    image_height          = 0;

    // Projector intrinsics
    double projector_matrix[9]      = {};
    double projector_distortion[5]  = {};
    int    projector_width          = 0;
    int    projector_height         = 0;

    // Stereo extrinsics (projector relative to camera)
    double rotation[9]    = {};   // 3x3 row-major
    double translation[3] = {};

    // Persistence
    Result<void> save(const std::string& path) const;
    static Result<CalibrationParams> load(const std::string& path);

    // Default parameters matching the SimCamera geometry
    static CalibrationParams default_sim();
};

// ---------------------------------------------------------------------------
// BoardParams -- checkerboard calibration target description
// ---------------------------------------------------------------------------
struct VXL_EXPORT BoardParams {
    int   cols = 9;              // inner corners horizontally
    int   rows = 6;              // inner corners vertically
    float square_size_mm = 25.0f;
};

// ---------------------------------------------------------------------------
// CalibrationResult -- output of a calibration run
// ---------------------------------------------------------------------------
struct VXL_EXPORT CalibrationResult {
    CalibrationParams params;
    double reprojection_error = 0.0;   // RMS in pixels, < 0.5 is good
    int    image_pairs_used   = 0;
};

// ---------------------------------------------------------------------------
// StereoCalibrator -- interactive calibration workflow
// ---------------------------------------------------------------------------
class VXL_EXPORT StereoCalibrator {
public:
    StereoCalibrator();
    ~StereoCalibrator();

    void set_board(const BoardParams& board);

    // Add image pair, auto-detect corners. Returns false if corners not found.
    Result<bool> add_image_pair(const Image& left, const Image& right);

    // Single camera: add one image
    Result<bool> add_image(const Image& image);

    int  pair_count() const;
    int  image_count() const;  // for single camera
    bool ready() const;        // enough images? (>= 10 pairs or >= 15 single)

    // Run stereo calibration
    Result<CalibrationResult> calibrate_stereo();

    // Run single camera calibration
    Result<CalibrationResult> calibrate_single();

    // Preview: detect corners in image, return image with corners drawn
    static Result<Image> detect_and_draw_corners(const Image& image,
                                                  const BoardParams& board);

    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
