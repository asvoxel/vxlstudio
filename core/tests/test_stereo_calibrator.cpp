// VxlStudio -- StereoCalibrator unit tests
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>

#include "vxl/calibration.h"
#include "vxl/types.h"

namespace vxl {
namespace {

// ---------------------------------------------------------------------------
// Helper: generate a synthetic checkerboard image with known corners
// ---------------------------------------------------------------------------
static Image make_checkerboard(int img_w, int img_h,
                                int board_cols, int board_rows,
                                int sq_px = 30,
                                int offset_x = 60, int offset_y = 60) {
    cv::Mat img(img_h, img_w, CV_8UC1, cv::Scalar(255));

    // Draw a checkerboard pattern: (board_cols+1) x (board_rows+1) squares
    int total_cols = board_cols + 1;
    int total_rows = board_rows + 1;

    for (int r = 0; r < total_rows; ++r) {
        for (int c = 0; c < total_cols; ++c) {
            if ((r + c) % 2 == 0) {
                cv::Rect rect(offset_x + c * sq_px,
                              offset_y + r * sq_px,
                              sq_px, sq_px);
                cv::rectangle(img, rect, cv::Scalar(0), cv::FILLED);
            }
        }
    }

    return Image::from_cv_mat(img);
}

// ---------------------------------------------------------------------------
// Test: detect_and_draw_corners with synthetic image
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, DetectAndDrawCorners) {
    const int cols = 7, rows = 5;
    BoardParams board;
    board.cols = cols;
    board.rows = rows;
    board.square_size_mm = 25.0f;

    auto img = make_checkerboard(640, 480, cols, rows, 30, 80, 60);
    ASSERT_GT(img.width, 0);
    ASSERT_GT(img.height, 0);

    auto result = StereoCalibrator::detect_and_draw_corners(img, board);
    EXPECT_TRUE(result.ok()) << result.message;
    if (result.ok()) {
        // Result image should be BGR (3-channel) with drawn corners
        EXPECT_EQ(result.value.width, img.width);
        EXPECT_EQ(result.value.height, img.height);
        EXPECT_EQ(result.value.format, PixelFormat::BGR8);
    }
}

// ---------------------------------------------------------------------------
// Test: add_image_pair with valid images
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, AddImagePairValidCorners) {
    const int cols = 7, rows = 5;
    BoardParams board;
    board.cols = cols;
    board.rows = rows;
    board.square_size_mm = 25.0f;

    StereoCalibrator cal;
    cal.set_board(board);

    // Use the same synthetic image as both left and right
    auto img = make_checkerboard(640, 480, cols, rows, 30, 80, 60);

    auto r = cal.add_image_pair(img, img);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_TRUE(r.value);  // corners should be found
    EXPECT_EQ(cal.pair_count(), 1);
}

// ---------------------------------------------------------------------------
// Test: add_image for single camera
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, AddImageSingle) {
    const int cols = 7, rows = 5;
    BoardParams board;
    board.cols = cols;
    board.rows = rows;

    StereoCalibrator cal;
    cal.set_board(board);

    auto img = make_checkerboard(640, 480, cols, rows, 30, 80, 60);

    auto r = cal.add_image(img);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value);
    EXPECT_EQ(cal.image_count(), 1);
}

// ---------------------------------------------------------------------------
// Test: ready() returns false with insufficient data
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, ReadyReturnsFalseWithFewPairs) {
    StereoCalibrator cal;
    BoardParams board;
    cal.set_board(board);

    EXPECT_FALSE(cal.ready());
    EXPECT_EQ(cal.pair_count(), 0);
    EXPECT_EQ(cal.image_count(), 0);

    // Add a few pairs -- still not enough
    auto img = make_checkerboard(640, 480, board.cols, board.rows, 20, 60, 40);
    for (int i = 0; i < 5; ++i) {
        cal.add_image_pair(img, img);
    }
    EXPECT_EQ(cal.pair_count(), 5);
    EXPECT_FALSE(cal.ready());
}

// ---------------------------------------------------------------------------
// Test: calibrate_stereo fails with insufficient data
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, CalibrateStereoFailsInsufficientData) {
    StereoCalibrator cal;
    BoardParams board;
    board.cols = 7;
    board.rows = 5;
    cal.set_board(board);

    // No images at all
    auto r = cal.calibrate_stereo();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, ErrorCode::CALIB_INSUFFICIENT_DATA);
}

// ---------------------------------------------------------------------------
// Test: calibrate_single fails with insufficient data
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, CalibrateSingleFailsInsufficientData) {
    StereoCalibrator cal;
    BoardParams board;
    board.cols = 7;
    board.rows = 5;
    cal.set_board(board);

    auto r = cal.calibrate_single();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, ErrorCode::CALIB_INSUFFICIENT_DATA);
}

// ---------------------------------------------------------------------------
// Test: clear() resets state
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, ClearResetsState) {
    const int cols = 7, rows = 5;
    BoardParams board;
    board.cols = cols;
    board.rows = rows;

    StereoCalibrator cal;
    cal.set_board(board);

    auto img = make_checkerboard(640, 480, cols, rows, 30, 80, 60);
    cal.add_image_pair(img, img);
    cal.add_image(img);

    EXPECT_EQ(cal.pair_count(), 1);
    EXPECT_EQ(cal.image_count(), 1);

    cal.clear();

    EXPECT_EQ(cal.pair_count(), 0);
    EXPECT_EQ(cal.image_count(), 0);
    EXPECT_FALSE(cal.ready());
}

// ---------------------------------------------------------------------------
// Test: add_image_pair returns false when no corners detected
// ---------------------------------------------------------------------------
TEST(StereoCalibrator, AddImagePairNoCorners) {
    BoardParams board;
    board.cols = 9;
    board.rows = 6;

    StereoCalibrator cal;
    cal.set_board(board);

    // Blank white image -- no corners
    auto blank = Image::create(640, 480, PixelFormat::GRAY8);
    std::memset(blank.buffer.data(), 200, static_cast<size_t>(blank.stride * blank.height));

    auto r = cal.add_image_pair(blank, blank);
    ASSERT_TRUE(r.ok());        // no error, just not found
    EXPECT_FALSE(r.value);      // corners not detected
    EXPECT_EQ(cal.pair_count(), 0);
}

} // namespace
} // namespace vxl
