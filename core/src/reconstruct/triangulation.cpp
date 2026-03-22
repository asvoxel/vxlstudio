// Internal module: Phase-to-3D coordinate conversion
//
// Triangulation approach:
//   - The unwrapped phase encodes the projector column position.
//     After multi-frequency temporal unwrapping at the highest frequency F,
//     the phase is continuous and spans approximately [-pi, 2*pi*F - pi].
//     The mapping to projector column is:
//       proj_col = phase / (2*pi) * (proj_width / F)
//     However, the caller normalizes the phase so that:
//       proj_col = phase / (2*pi) * proj_width
//
//   - Once we have (camera_pixel, projector_col), we triangulate:
//     Camera ray: K_cam^{-1} * [u, v, 1]^T  (in camera frame)
//     Projector plane: defined by projector_col and projector intrinsics+extrinsics
//     Solve for the ray parameter t that satisfies the projector constraint.

#include "triangulation.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

namespace vxl::detail {

namespace {

// Stereo geometry helper for camera-projector triangulation.
struct StereoGeometry {
    cv::Matx33d K_cam_inv;
    cv::Matx33d K_proj;
    cv::Matx33d R;
    cv::Vec3d T;
    int proj_width;
    int proj_height;

    static StereoGeometry from_calib(const CalibrationParams& calib) {
        StereoGeometry g;

        cv::Matx33d K_cam(
            calib.camera_matrix[0], calib.camera_matrix[1], calib.camera_matrix[2],
            calib.camera_matrix[3], calib.camera_matrix[4], calib.camera_matrix[5],
            calib.camera_matrix[6], calib.camera_matrix[7], calib.camera_matrix[8]);
        g.K_cam_inv = K_cam.inv();

        g.K_proj = cv::Matx33d(
            calib.projector_matrix[0], calib.projector_matrix[1], calib.projector_matrix[2],
            calib.projector_matrix[3], calib.projector_matrix[4], calib.projector_matrix[5],
            calib.projector_matrix[6], calib.projector_matrix[7], calib.projector_matrix[8]);

        g.R = cv::Matx33d(
            calib.rotation[0], calib.rotation[1], calib.rotation[2],
            calib.rotation[3], calib.rotation[4], calib.rotation[5],
            calib.rotation[6], calib.rotation[7], calib.rotation[8]);

        g.T = cv::Vec3d(calib.translation[0], calib.translation[1], calib.translation[2]);

        g.proj_width = calib.projector_width;
        g.proj_height = calib.projector_height;
        return g;
    }

    // Triangulate a single point.
    //
    // Camera ray: X_cam = t * d, where d = K_cam^{-1} * [u,v,1]^T
    // Projector constraint: X_proj = R * X_cam + T, and the x-component
    //   in projector image coordinates must equal the known projector column.
    //
    //   X_proj = t * R * d + T
    //   proj_col = (X_proj[0] * fx_proj / X_proj[2]) + cx_proj
    //
    // Solving for t:
    //   Let r = R * d. Then X_proj = t*r + T.
    //   proj_col = fx * (t*r[0] + T[0]) / (t*r[2] + T[2]) + cx
    //   (proj_col - cx) * (t*r[2] + T[2]) = fx * (t*r[0] + T[0])
    //   t * ((proj_col-cx)*r[2] - fx*r[0]) = fx*T[0] - (proj_col-cx)*T[2]
    //   t = (fx*T[0] - (p-cx)*T[2]) / ((p-cx)*r[2] - fx*r[0])
    cv::Vec3f triangulate_point(double u, double v, double proj_col) const {
        cv::Vec3d d = K_cam_inv * cv::Vec3d(u, v, 1.0);
        cv::Vec3d r = R * d;

        double fx = K_proj(0, 0);
        double cx = K_proj(0, 2);
        double p_cx = proj_col - cx;

        double denominator = p_cx * r[2] - fx * r[0];

        if (std::abs(denominator) < 1e-10) {
            float nan_val = std::numeric_limits<float>::quiet_NaN();
            return cv::Vec3f(nan_val, nan_val, nan_val);
        }

        double numerator = fx * T[0] - p_cx * T[2];
        double t = numerator / denominator;

        // Reject points behind the camera
        if (t <= 0.0) {
            float nan_val = std::numeric_limits<float>::quiet_NaN();
            return cv::Vec3f(nan_val, nan_val, nan_val);
        }

        cv::Vec3d X = t * d;

        return cv::Vec3f(static_cast<float>(X[0]),
                         static_cast<float>(X[1]),
                         static_cast<float>(X[2]));
    }
};

} // anonymous namespace

PointCloud phase_to_3d(
    const cv::Mat& unwrapped_phase,
    const CalibrationParams& calib,
    const cv::Mat& validity_mask)
{
    const int rows = unwrapped_phase.rows;
    const int cols = unwrapped_phase.cols;
    const float two_pi = static_cast<float>(2.0 * CV_PI);

    StereoGeometry geom = StereoGeometry::from_calib(calib);

    // Collect valid 3D points
    std::vector<cv::Vec3f> points;
    points.reserve(static_cast<size_t>(rows) * cols / 4);

    const bool has_mask = !validity_mask.empty();

    // Maximum reasonable Z distance (10 meters) to reject outliers from
    // degenerate triangulation geometry.
    const float max_z = 10000.0f;

    for (int r = 0; r < rows; ++r) {
        const float* phase_row = unwrapped_phase.ptr<float>(r);
        const uint8_t* mask_row = has_mask ? validity_mask.ptr<uint8_t>(r) : nullptr;

        for (int c = 0; c < cols; ++c) {
            if (has_mask && mask_row[c] == 0) continue;
            if (std::isnan(phase_row[c])) continue;

            // Bounds check: skip pixels outside the calibrated image area
            if (c < 0 || c >= calib.image_width ||
                r < 0 || r >= calib.image_height) {
                continue;
            }

            float phase = phase_row[c];

            // Convert unwrapped phase to projector column.
            // The unwrapped phase is in radians; for a pattern with base
            // frequency 1 (one period = full projector width), after temporal
            // unwrapping, phase maps linearly to projector column:
            //   proj_col = phase / (2*pi) * projector_width
            // Note: the phase from atan2 is in [-pi, pi], and after
            // unwrapping spans [0, 2*pi) for one full period.
            double proj_col = static_cast<double>(phase) / two_pi
                              * calib.projector_width;

            // Skip projector columns that fall outside the projector image
            if (proj_col < 0.0 || proj_col >= calib.projector_width) {
                continue;
            }

            cv::Vec3f pt = geom.triangulate_point(c, r, proj_col);
            if (!std::isnan(pt[0])) {
                // Reject points with unreasonable Z values
                if (std::abs(pt[2]) > max_z) continue;
                points.push_back(pt);
            }
        }
    }

    // Build PointCloud
    PointCloud cloud;
    cloud.format = PointFormat::XYZ_FLOAT;
    cloud.point_count = points.size();
    if (!points.empty()) {
        cloud.buffer = SharedBuffer::allocate(points.size() * 3 * sizeof(float));
        std::memcpy(cloud.buffer.data(), points.data(),
                    points.size() * 3 * sizeof(float));
    }

    return cloud;
}

} // namespace vxl::detail
