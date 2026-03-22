// Stereo matching reconstruction
//
// Pipeline:
//   1. Convert Images to cv::Mat
//   2. stereoRectify to compute rectification transforms
//   3. initUndistortRectifyMap + remap for both cameras
//   4. StereoSGBM disparity computation
//   5. reprojectImageTo3D
//   6. Filter invalid points (Z <= 0 or too large)
//   7. Build PointCloud and HeightMap

#include "stereo_match.h"
#include "height_map_gen.h"

#include <cmath>
#include <cstring>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl::detail {

static Result<ReconstructOutput> stereo_to_cloud_impl(
    const Image& left, const Image& right,
    const CalibrationParams& calib, const ReconstructParams& params);

Result<ReconstructOutput> stereo_to_cloud(
    const Image& left,
    const Image& right,
    const CalibrationParams& calib,
    const ReconstructParams& params)
{
    try {
    return stereo_to_cloud_impl(left, right, calib, params);
    } catch (const cv::Exception& e) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            std::string("OpenCV stereo error: ") + e.what());
    }
}

static Result<ReconstructOutput> stereo_to_cloud_impl(
    const Image& left,
    const Image& right,
    const CalibrationParams& calib,
    const ReconstructParams& params)
{
    // -----------------------------------------------------------------------
    // 1. Convert Images to cv::Mat
    // -----------------------------------------------------------------------
    cv::Mat left_mat = left.to_cv_mat();
    cv::Mat right_mat = right.to_cv_mat();

    // Ensure grayscale
    if (left_mat.channels() > 1) {
        cv::cvtColor(left_mat, left_mat, cv::COLOR_BGR2GRAY);
    }
    if (right_mat.channels() > 1) {
        cv::cvtColor(right_mat, right_mat, cv::COLOR_BGR2GRAY);
    }

    // Convert to 8-bit if needed
    if (left_mat.type() != CV_8U) {
        left_mat.convertTo(left_mat, CV_8U);
    }
    if (right_mat.type() != CV_8U) {
        right_mat.convertTo(right_mat, CV_8U);
    }

    if (left_mat.size() != right_mat.size()) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Left and right images must have the same dimensions");
    }

    cv::Size imageSize = left_mat.size();

    // -----------------------------------------------------------------------
    // 2. Build intrinsic/extrinsic matrices from CalibrationParams
    // -----------------------------------------------------------------------
    // Left camera intrinsics
    cv::Mat cam_left_K = (cv::Mat_<double>(3, 3) <<
        calib.camera_matrix[0], calib.camera_matrix[1], calib.camera_matrix[2],
        calib.camera_matrix[3], calib.camera_matrix[4], calib.camera_matrix[5],
        calib.camera_matrix[6], calib.camera_matrix[7], calib.camera_matrix[8]);

    cv::Mat cam_left_dist = (cv::Mat_<double>(1, 5) <<
        calib.camera_distortion[0], calib.camera_distortion[1],
        calib.camera_distortion[2], calib.camera_distortion[3],
        calib.camera_distortion[4]);

    // Right camera intrinsics (stored in projector fields)
    cv::Mat cam_right_K = (cv::Mat_<double>(3, 3) <<
        calib.projector_matrix[0], calib.projector_matrix[1], calib.projector_matrix[2],
        calib.projector_matrix[3], calib.projector_matrix[4], calib.projector_matrix[5],
        calib.projector_matrix[6], calib.projector_matrix[7], calib.projector_matrix[8]);

    cv::Mat cam_right_dist = (cv::Mat_<double>(1, 5) <<
        calib.projector_distortion[0], calib.projector_distortion[1],
        calib.projector_distortion[2], calib.projector_distortion[3],
        calib.projector_distortion[4]);

    // Rotation and translation (right relative to left)
    cv::Mat R = (cv::Mat_<double>(3, 3) <<
        calib.rotation[0], calib.rotation[1], calib.rotation[2],
        calib.rotation[3], calib.rotation[4], calib.rotation[5],
        calib.rotation[6], calib.rotation[7], calib.rotation[8]);

    cv::Mat T = (cv::Mat_<double>(3, 1) <<
        calib.translation[0],
        calib.translation[1],
        calib.translation[2]);

    // -----------------------------------------------------------------------
    // 3. Stereo rectification
    // -----------------------------------------------------------------------
    cv::Mat R1, R2, P1, P2, Q;
    cv::stereoRectify(
        cam_left_K, cam_left_dist,
        cam_right_K, cam_right_dist,
        imageSize, R, T,
        R1, R2, P1, P2, Q,
        cv::CALIB_ZERO_DISPARITY, -1, imageSize);

    // -----------------------------------------------------------------------
    // 4. Undistort and rectify both images
    // -----------------------------------------------------------------------
    cv::Mat map1_left, map2_left, map1_right, map2_right;

    cv::initUndistortRectifyMap(
        cam_left_K, cam_left_dist, R1, P1,
        imageSize, CV_32FC1, map1_left, map2_left);

    cv::initUndistortRectifyMap(
        cam_right_K, cam_right_dist, R2, P2,
        imageSize, CV_32FC1, map1_right, map2_right);

    cv::Mat rect_left, rect_right;
    cv::remap(left_mat, rect_left, map1_left, map2_left, cv::INTER_LINEAR);
    cv::remap(right_mat, rect_right, map1_right, map2_right, cv::INTER_LINEAR);

    // -----------------------------------------------------------------------
    // 5. Stereo matching with StereoSGBM
    // -----------------------------------------------------------------------
    const int minDisparity = 0;
    const int numDisparities = 128;  // must be divisible by 16
    const int blockSize = 5;

    auto sgbm = cv::StereoSGBM::create(
        minDisparity,
        numDisparities,
        blockSize,
        8 * blockSize * blockSize,     // P1
        32 * blockSize * blockSize,    // P2
        1,                              // disp12MaxDiff
        0,                              // preFilterCap (0 = default)
        10,                             // uniquenessRatio
        100,                            // speckleWindowSize
        32,                             // speckleRange
        cv::StereoSGBM::MODE_SGBM_3WAY);

    cv::Mat disparity;
    sgbm->compute(rect_left, rect_right, disparity);
    // disparity is CV_16S with values in fixed-point 1/16 pixel units

    // -----------------------------------------------------------------------
    // 6. Reproject to 3D
    // -----------------------------------------------------------------------
    cv::Mat points3D;
    cv::reprojectImageTo3D(disparity, points3D, Q, /*handleMissingValues=*/true);
    // points3D is CV_32FC3

    // -----------------------------------------------------------------------
    // 7. Extract valid 3D points
    // -----------------------------------------------------------------------
    const float max_depth = 10000.0f;  // 10 meters max
    std::vector<float> valid_points;
    valid_points.reserve(static_cast<size_t>(imageSize.width) * imageSize.height * 3);

    cv::Mat validity(imageSize, CV_8U, cv::Scalar(0));

    for (int v = 0; v < points3D.rows; ++v) {
        const cv::Vec3f* row = points3D.ptr<cv::Vec3f>(v);
        const int16_t* disp_row = disparity.ptr<int16_t>(v);
        uint8_t* mask_row = validity.ptr<uint8_t>(v);

        for (int u = 0; u < points3D.cols; ++u) {
            // StereoSGBM marks invalid disparities as minDisparity - 1 (i.e., -16 in fixed-point)
            // After reprojectImageTo3D with handleMissingValues=true, invalid points get very large Z.
            int16_t d = disp_row[u];
            if (d <= minDisparity * 16) {
                continue;  // invalid disparity
            }

            float Z = row[u][2];
            if (Z > 0.0f && Z < max_depth && std::isfinite(Z)) {
                valid_points.push_back(row[u][0]);
                valid_points.push_back(row[u][1]);
                valid_points.push_back(Z);
                mask_row[u] = 255;
            }
        }
    }

    size_t point_count = valid_points.size() / 3;

    // Build PointCloud (may be empty if stereo matching found no valid disparities)
    PointCloud cloud;
    cloud.format = PointFormat::XYZ_FLOAT;
    cloud.point_count = point_count;
    if (point_count > 0) {
        cloud.buffer = SharedBuffer::allocate(point_count * 3 * sizeof(float));
        std::memcpy(cloud.buffer.data(), valid_points.data(),
                     point_count * 3 * sizeof(float));
    }

    // -----------------------------------------------------------------------
    // 8. Generate height map
    // -----------------------------------------------------------------------
    HeightMap hmap;
    if (point_count > 0) {
        hmap = cloud_to_height_map(cloud, params.height_map_resolution_mm);
    }

    // -----------------------------------------------------------------------
    // 9. Package output
    // -----------------------------------------------------------------------
    ReconstructOutput output;
    output.point_cloud = std::move(cloud);
    output.height_map = std::move(hmap);
    output.modulation_mask = Image::from_cv_mat(validity);

    return Result<ReconstructOutput>::success(std::move(output));
}

} // namespace vxl::detail
