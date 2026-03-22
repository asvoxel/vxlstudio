// Tests for multi-source 3D acquisition: depth_direct, stereo, and process dispatcher
//
// Covers:
//   1. from_depth with synthetic GRAY16 depth map (uniform Z=500mm)
//   2. from_depth with zero/negative depth pixels (should be skipped)
//   3. from_depth with FLOAT32 depth map
//   4. from_stereo with identical left/right (expect mostly invalid but no crash)
//   5. process("depth_direct", ...) dispatches correctly
//   6. process("unknown_type", ...) returns error

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include <opencv2/core.hpp>

#include "vxl/calibration.h"
#include "vxl/error.h"
#include "vxl/reconstruct.h"
#include "vxl/types.h"

namespace {

// ---------------------------------------------------------------------------
// Helper: create CalibrationParams with simple pinhole intrinsics
// ---------------------------------------------------------------------------
vxl::CalibrationParams make_depth_test_calib(int img_w, int img_h)
{
    vxl::CalibrationParams calib;

    double fx = 500.0, fy = 500.0;
    double cx = img_w / 2.0, cy = img_h / 2.0;

    // Camera intrinsics
    calib.camera_matrix[0] = fx;
    calib.camera_matrix[1] = 0;
    calib.camera_matrix[2] = cx;
    calib.camera_matrix[3] = 0;
    calib.camera_matrix[4] = fy;
    calib.camera_matrix[5] = cy;
    calib.camera_matrix[6] = 0;
    calib.camera_matrix[7] = 0;
    calib.camera_matrix[8] = 1;

    calib.image_width  = img_w;
    calib.image_height = img_h;

    // Projector/right-camera intrinsics (same as camera for simplicity)
    calib.projector_matrix[0] = fx;
    calib.projector_matrix[1] = 0;
    calib.projector_matrix[2] = cx;
    calib.projector_matrix[3] = 0;
    calib.projector_matrix[4] = fy;
    calib.projector_matrix[5] = cy;
    calib.projector_matrix[6] = 0;
    calib.projector_matrix[7] = 0;
    calib.projector_matrix[8] = 1;

    calib.projector_width  = img_w;
    calib.projector_height = img_h;

    // Identity rotation
    calib.rotation[0] = 1; calib.rotation[1] = 0; calib.rotation[2] = 0;
    calib.rotation[3] = 0; calib.rotation[4] = 1; calib.rotation[5] = 0;
    calib.rotation[6] = 0; calib.rotation[7] = 0; calib.rotation[8] = 1;

    // Horizontal baseline of 50mm (for stereo)
    calib.translation[0] = 50.0;
    calib.translation[1] = 0.0;
    calib.translation[2] = 0.0;

    return calib;
}

// ---------------------------------------------------------------------------
// Test 1: from_depth with synthetic GRAY16 depth map at Z=500mm
// ---------------------------------------------------------------------------
TEST(DepthDirect, UniformGray16DepthAt500mm)
{
    const int W = 100;
    const int H = 100;
    const uint16_t depth_mm = 500;

    // Create a GRAY16 depth map with uniform depth
    cv::Mat depth_mat(H, W, CV_16U, cv::Scalar(depth_mm));
    vxl::Image depth_img = vxl::Image::from_cv_mat(depth_mat);

    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    auto result = vxl::Reconstruct::from_depth(depth_img, calib, 5000.0f);

    ASSERT_TRUE(result.ok()) << "from_depth failed: " << result.message;

    // Should have points (100x100 = 10000 valid pixels)
    EXPECT_GT(result.value.point_cloud.point_count, 0u);
    EXPECT_EQ(result.value.point_cloud.point_count, static_cast<size_t>(W * H));

    // Verify all Z values are ~500mm
    const float* pts = reinterpret_cast<const float*>(
        result.value.point_cloud.buffer.data());
    size_t n = result.value.point_cloud.point_count;

    for (size_t i = 0; i < n; ++i) {
        float z = pts[i * 3 + 2];
        EXPECT_NEAR(z, 500.0f, 0.1f)
            << "Point " << i << " Z should be ~500mm";
    }

    // Check that center pixel (50,50) maps to approximately (0, 0, 500)
    // since cx=50, cy=50, so X = (50-50)*500/500 = 0, Y = (50-50)*500/500 = 0
    // Find a center-ish point
    bool found_center = false;
    for (size_t i = 0; i < n; ++i) {
        float x = pts[i * 3 + 0];
        float y = pts[i * 3 + 1];
        float z = pts[i * 3 + 2];
        if (std::abs(x) < 0.5f && std::abs(y) < 0.5f) {
            EXPECT_NEAR(z, 500.0f, 0.1f);
            found_center = true;
            break;
        }
    }
    EXPECT_TRUE(found_center) << "Should have a point near the optical axis";
}

// ---------------------------------------------------------------------------
// Test 2: from_depth with zero depth pixels (should be skipped)
// ---------------------------------------------------------------------------
TEST(DepthDirect, ZeroDepthPixelsSkipped)
{
    const int W = 100;
    const int H = 100;

    // Create depth map: left half = 0 (invalid), right half = 800mm
    cv::Mat depth_mat(H, W, CV_16U, cv::Scalar(0));
    for (int r = 0; r < H; ++r) {
        uint16_t* row = depth_mat.ptr<uint16_t>(r);
        for (int c = W / 2; c < W; ++c) {
            row[c] = 800;
        }
    }

    vxl::Image depth_img = vxl::Image::from_cv_mat(depth_mat);
    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    auto result = vxl::Reconstruct::from_depth(depth_img, calib, 5000.0f);

    ASSERT_TRUE(result.ok()) << "from_depth failed: " << result.message;

    // Only right half should have points: 100 * 50 = 5000
    EXPECT_EQ(result.value.point_cloud.point_count, static_cast<size_t>(H * (W / 2)));

    // All Z should be ~800mm
    const float* pts = reinterpret_cast<const float*>(
        result.value.point_cloud.buffer.data());
    size_t n = result.value.point_cloud.point_count;
    for (size_t i = 0; i < n; ++i) {
        float z = pts[i * 3 + 2];
        EXPECT_NEAR(z, 800.0f, 0.1f);
    }
}

// ---------------------------------------------------------------------------
// Test 3: from_depth with FLOAT32 depth map
// ---------------------------------------------------------------------------
TEST(DepthDirect, Float32DepthMap)
{
    const int W = 50;
    const int H = 50;
    const float depth_mm = 1234.5f;

    cv::Mat depth_mat(H, W, CV_32F, cv::Scalar(depth_mm));
    vxl::Image depth_img = vxl::Image::from_cv_mat(depth_mat);

    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    auto result = vxl::Reconstruct::from_depth(depth_img, calib, 5000.0f);

    ASSERT_TRUE(result.ok()) << "from_depth failed: " << result.message;
    EXPECT_EQ(result.value.point_cloud.point_count, static_cast<size_t>(W * H));

    const float* pts = reinterpret_cast<const float*>(
        result.value.point_cloud.buffer.data());
    for (size_t i = 0; i < result.value.point_cloud.point_count; ++i) {
        EXPECT_NEAR(pts[i * 3 + 2], depth_mm, 0.01f);
    }
}

// ---------------------------------------------------------------------------
// Test 4: from_depth rejects depth beyond max_depth
// ---------------------------------------------------------------------------
TEST(DepthDirect, BeyondMaxDepthSkipped)
{
    const int W = 10;
    const int H = 10;

    // All pixels at 6000mm, max_depth = 5000
    cv::Mat depth_mat(H, W, CV_16U, cv::Scalar(6000));
    vxl::Image depth_img = vxl::Image::from_cv_mat(depth_mat);

    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    auto result = vxl::Reconstruct::from_depth(depth_img, calib, 5000.0f);

    // Should fail because no valid pixels
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, vxl::ErrorCode::RECONSTRUCT_LOW_MODULATION);
}

// ---------------------------------------------------------------------------
// Test 5: from_stereo with identical images (should not crash)
// ---------------------------------------------------------------------------
TEST(StereoMatch, IdenticalImagesDoNotCrash)
{
    const int W = 128;
    const int H = 128;

    // Create a simple gradient image
    cv::Mat gray(H, W, CV_8U);
    for (int r = 0; r < H; ++r) {
        uint8_t* row = gray.ptr<uint8_t>(r);
        for (int c = 0; c < W; ++c) {
            row[c] = static_cast<uint8_t>(c * 255 / W);
        }
    }

    vxl::Image left = vxl::Image::from_cv_mat(gray);
    vxl::Image right = vxl::Image::from_cv_mat(gray);

    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    vxl::ReconstructParams params;
    params.height_map_resolution_mm = 1.0f;

    // This should not crash. With identical images and identical intrinsics,
    // stereo matching will produce mostly zero disparity / invalid results.
    auto result = vxl::Reconstruct::from_stereo(left, right, calib, params);

    // We accept either success (with possibly 0 points) or a non-crash failure
    // The important thing is it does not crash or throw.
    if (result.ok()) {
        // If it succeeds, point count should be >= 0
        EXPECT_GE(result.value.point_cloud.point_count, 0u);
    }
    // If it fails, that's also acceptable for degenerate stereo input
}

// ---------------------------------------------------------------------------
// Test 6: process("depth_direct", ...) dispatches correctly
// ---------------------------------------------------------------------------
TEST(ProcessDispatch, DepthDirectDispatch)
{
    const int W = 20;
    const int H = 20;
    const uint16_t depth_mm = 300;

    cv::Mat depth_mat(H, W, CV_16U, cv::Scalar(depth_mm));
    vxl::Image depth_img = vxl::Image::from_cv_mat(depth_mat);

    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    std::vector<vxl::Image> images = { depth_img };

    auto result = vxl::Reconstruct::process("depth_direct", images, calib);

    ASSERT_TRUE(result.ok()) << "process(depth_direct) failed: " << result.message;
    EXPECT_EQ(result.value.point_cloud.point_count, static_cast<size_t>(W * H));

    // Verify Z values
    const float* pts = reinterpret_cast<const float*>(
        result.value.point_cloud.buffer.data());
    for (size_t i = 0; i < result.value.point_cloud.point_count; ++i) {
        EXPECT_NEAR(pts[i * 3 + 2], static_cast<float>(depth_mm), 0.1f);
    }
}

// ---------------------------------------------------------------------------
// Test 7: process("unknown_type", ...) returns INVALID_PARAMETER error
// ---------------------------------------------------------------------------
TEST(ProcessDispatch, UnknownTypeReturnsError)
{
    vxl::CalibrationParams calib = make_depth_test_calib(64, 64);
    std::vector<vxl::Image> images;

    auto result = vxl::Reconstruct::process("unknown_type", images, calib);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Test 8: process("stereo", ...) with insufficient images returns error
// ---------------------------------------------------------------------------
TEST(ProcessDispatch, StereoRequiresTwoImages)
{
    vxl::CalibrationParams calib = make_depth_test_calib(64, 64);
    std::vector<vxl::Image> images; // empty

    auto result = vxl::Reconstruct::process("stereo", images, calib);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Test 9: from_depth rejects invalid format (GRAY8)
// ---------------------------------------------------------------------------
TEST(DepthDirect, RejectsGray8Format)
{
    const int W = 10;
    const int H = 10;

    cv::Mat gray8(H, W, CV_8U, cv::Scalar(100));
    vxl::Image img = vxl::Image::from_cv_mat(gray8);

    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    auto result = vxl::Reconstruct::from_depth(img, calib, 5000.0f);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Test 10: Verify X,Y coordinates from depth map are geometrically correct
// ---------------------------------------------------------------------------
TEST(DepthDirect, XYCoordinatesCorrect)
{
    const int W = 100;
    const int H = 100;
    const float depth_mm = 1000.0f;
    const double fx = 500.0, fy = 500.0;
    const double cx = 50.0, cy = 50.0;

    cv::Mat depth_mat(H, W, CV_32F, cv::Scalar(depth_mm));
    vxl::Image depth_img = vxl::Image::from_cv_mat(depth_mat);

    vxl::CalibrationParams calib = make_depth_test_calib(W, H);

    auto result = vxl::Reconstruct::from_depth(depth_img, calib, 5000.0f);
    ASSERT_TRUE(result.ok());

    const float* pts = reinterpret_cast<const float*>(
        result.value.point_cloud.buffer.data());
    size_t n = result.value.point_cloud.point_count;

    // Points are generated row by row, left to right.
    // Pixel (0, 0) -> X = (0 - 50) * 1000 / 500 = -100, Y = (0 - 50) * 1000 / 500 = -100
    // First point should be pixel (0, 0)
    ASSERT_GT(n, 0u);
    float x0 = pts[0];
    float y0 = pts[1];
    float z0 = pts[2];

    float expected_x = static_cast<float>((0 - cx) * depth_mm / fx);  // -100
    float expected_y = static_cast<float>((0 - cy) * depth_mm / fy);  // -100

    EXPECT_NEAR(x0, expected_x, 0.01f);
    EXPECT_NEAR(y0, expected_y, 0.01f);
    EXPECT_NEAR(z0, depth_mm, 0.01f);

    // Last point should be pixel (W-1, H-1)
    float xn = pts[(n - 1) * 3 + 0];
    float yn = pts[(n - 1) * 3 + 1];

    float expected_xn = static_cast<float>(((W - 1) - cx) * depth_mm / fx);  // 98
    float expected_yn = static_cast<float>(((H - 1) - cy) * depth_mm / fy);  // 98

    EXPECT_NEAR(xn, expected_xn, 0.01f);
    EXPECT_NEAR(yn, expected_yn, 0.01f);
}

} // namespace
