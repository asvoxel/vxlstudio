// VxlStudio -- StereoCalibrator implementation
// SPDX-License-Identifier: MIT

#include "vxl/calibration.h"

#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct StereoCalibrator::Impl {
    BoardParams board;

    // Stereo pairs
    std::vector<std::vector<cv::Point2f>> left_corners;
    std::vector<std::vector<cv::Point2f>> right_corners;
    cv::Size stereo_image_size;

    // Single camera
    std::vector<std::vector<cv::Point2f>> single_corners;
    cv::Size single_image_size;

    // Build the 3D object-point grid for one board pose
    std::vector<cv::Point3f> make_object_points() const {
        std::vector<cv::Point3f> pts;
        pts.reserve(static_cast<size_t>(board.rows * board.cols));
        for (int r = 0; r < board.rows; ++r) {
            for (int c = 0; c < board.cols; ++c) {
                pts.emplace_back(
                    static_cast<float>(c) * board.square_size_mm,
                    static_cast<float>(r) * board.square_size_mm,
                    0.0f);
            }
        }
        return pts;
    }

    // Try to find corners in a single image (grayscale conversion if needed)
    static bool find_corners(const Image& img, const BoardParams& brd,
                             std::vector<cv::Point2f>& corners,
                             cv::Mat& gray_out) {
        cv::Mat mat = img.to_cv_mat();
        cv::Mat gray;
        if (mat.channels() == 3) {
            cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
        } else if (mat.channels() == 1) {
            gray = mat;
        } else {
            return false;
        }

        cv::Size pattern(brd.cols, brd.rows);
        bool found = cv::findChessboardCorners(
            gray, pattern, corners,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE |
                cv::CALIB_CB_FAST_CHECK);

        if (found) {
            cv::cornerSubPix(
                gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
                cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
                                 30, 0.001));
        }

        gray_out = gray;
        return found;
    }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

StereoCalibrator::StereoCalibrator() : impl_(std::make_unique<Impl>()) {}

StereoCalibrator::~StereoCalibrator() = default;

void StereoCalibrator::set_board(const BoardParams& board) {
    impl_->board = board;
}

Result<bool> StereoCalibrator::add_image_pair(const Image& left,
                                               const Image& right) {
    std::vector<cv::Point2f> lc, rc;
    cv::Mat gray_l, gray_r;
    bool found_l = Impl::find_corners(left, impl_->board, lc, gray_l);
    bool found_r = Impl::find_corners(right, impl_->board, rc, gray_r);

    if (!found_l || !found_r) {
        return Result<bool>::success(false);
    }

    impl_->left_corners.push_back(std::move(lc));
    impl_->right_corners.push_back(std::move(rc));
    impl_->stereo_image_size = cv::Size(left.width, left.height);

    return Result<bool>::success(true);
}

Result<bool> StereoCalibrator::add_image(const Image& image) {
    std::vector<cv::Point2f> corners;
    cv::Mat gray;
    bool found = Impl::find_corners(image, impl_->board, corners, gray);
    if (!found) {
        return Result<bool>::success(false);
    }

    impl_->single_corners.push_back(std::move(corners));
    impl_->single_image_size = cv::Size(image.width, image.height);

    return Result<bool>::success(true);
}

int StereoCalibrator::pair_count() const {
    return static_cast<int>(impl_->left_corners.size());
}

int StereoCalibrator::image_count() const {
    return static_cast<int>(impl_->single_corners.size());
}

bool StereoCalibrator::ready() const {
    return pair_count() >= 10 || image_count() >= 15;
}

Result<CalibrationResult> StereoCalibrator::calibrate_stereo() {
    const int n = pair_count();
    if (n < 3) {
        return Result<CalibrationResult>::failure(
            ErrorCode::CALIB_INSUFFICIENT_DATA,
            "Need at least 3 image pairs for stereo calibration (have " +
                std::to_string(n) + ")");
    }

    // Build object points (same grid for every image)
    auto obj_one = impl_->make_object_points();
    std::vector<std::vector<cv::Point3f>> object_points(
        static_cast<size_t>(n), obj_one);

    cv::Mat cam_l = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat cam_r = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat dist_l, dist_r;
    cv::Mat R, T, E, F;

    double rms = cv::stereoCalibrate(
        object_points,
        impl_->left_corners, impl_->right_corners,
        cam_l, dist_l, cam_r, dist_r,
        impl_->stereo_image_size, R, T, E, F,
        cv::CALIB_FIX_INTRINSIC * 0,   // let it optimize intrinsics too
        cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
                         100, 1e-6));

    // Fill result
    CalibrationResult result;
    result.reprojection_error = rms;
    result.image_pairs_used   = n;

    auto& p = result.params;
    p.image_width  = impl_->stereo_image_size.width;
    p.image_height = impl_->stereo_image_size.height;
    p.projector_width  = impl_->stereo_image_size.width;
    p.projector_height = impl_->stereo_image_size.height;

    // Camera matrix (left) -> camera_matrix
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.camera_matrix[i * 3 + j] = cam_l.at<double>(i, j);

    // Camera distortion (left)
    for (int i = 0; i < std::min(5, dist_l.cols * dist_l.rows); ++i)
        p.camera_distortion[i] = dist_l.at<double>(i);

    // Projector matrix (right) -> projector_matrix
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.projector_matrix[i * 3 + j] = cam_r.at<double>(i, j);

    // Projector distortion (right)
    for (int i = 0; i < std::min(5, dist_r.cols * dist_r.rows); ++i)
        p.projector_distortion[i] = dist_r.at<double>(i);

    // Rotation
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.rotation[i * 3 + j] = R.at<double>(i, j);

    // Translation
    for (int i = 0; i < 3; ++i)
        p.translation[i] = T.at<double>(i);

    return Result<CalibrationResult>::success(std::move(result));
}

Result<CalibrationResult> StereoCalibrator::calibrate_single() {
    const int n = image_count();
    if (n < 3) {
        return Result<CalibrationResult>::failure(
            ErrorCode::CALIB_INSUFFICIENT_DATA,
            "Need at least 3 images for single camera calibration (have " +
                std::to_string(n) + ")");
    }

    auto obj_one = impl_->make_object_points();
    std::vector<std::vector<cv::Point3f>> object_points(
        static_cast<size_t>(n), obj_one);

    cv::Mat cam_mat = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat dist_coeffs;
    std::vector<cv::Mat> rvecs, tvecs;

    double rms = cv::calibrateCamera(
        object_points, impl_->single_corners,
        impl_->single_image_size,
        cam_mat, dist_coeffs, rvecs, tvecs);

    CalibrationResult result;
    result.reprojection_error = rms;
    result.image_pairs_used   = n;

    auto& p = result.params;
    p.image_width  = impl_->single_image_size.width;
    p.image_height = impl_->single_image_size.height;

    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.camera_matrix[i * 3 + j] = cam_mat.at<double>(i, j);

    for (int i = 0; i < std::min(5, dist_coeffs.cols * dist_coeffs.rows); ++i)
        p.camera_distortion[i] = dist_coeffs.at<double>(i);

    return Result<CalibrationResult>::success(std::move(result));
}

Result<Image> StereoCalibrator::detect_and_draw_corners(const Image& image,
                                                         const BoardParams& board) {
    cv::Mat mat = image.to_cv_mat();
    cv::Mat gray;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    } else if (mat.channels() == 1) {
        gray = mat;
    } else {
        return Result<Image>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Unsupported image format for corner detection");
    }

    cv::Size pattern(board.cols, board.rows);
    std::vector<cv::Point2f> corners;
    bool found = cv::findChessboardCorners(
        gray, pattern, corners,
        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE |
            cv::CALIB_CB_FAST_CHECK);

    if (found) {
        cv::cornerSubPix(
            gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
                             30, 0.001));
    }

    // Draw on a color copy
    cv::Mat color;
    if (mat.channels() == 1) {
        cv::cvtColor(mat, color, cv::COLOR_GRAY2BGR);
    } else {
        color = mat.clone();
    }

    cv::drawChessboardCorners(color, pattern, corners, found);

    return Result<Image>::success(Image::from_cv_mat(color));
}

void StereoCalibrator::clear() {
    impl_->left_corners.clear();
    impl_->right_corners.clear();
    impl_->single_corners.clear();
}

} // namespace vxl
