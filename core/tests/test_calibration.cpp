#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "vxl/calibration.h"

namespace vxl {
namespace {

TEST(Calibration, DefaultSimReturnsValidParams) {
    auto p = CalibrationParams::default_sim();

    // Camera matrix should have positive focal lengths
    EXPECT_GT(p.camera_matrix[0], 0.0);  // fx
    EXPECT_GT(p.camera_matrix[4], 0.0);  // fy
    EXPECT_EQ(p.camera_matrix[8], 1.0);  // homogeneous

    // Dimensions should be positive
    EXPECT_GT(p.image_width, 0);
    EXPECT_GT(p.image_height, 0);
    EXPECT_GT(p.projector_width, 0);
    EXPECT_GT(p.projector_height, 0);

    // Rotation should be identity-like (det ~1)
    double det = p.rotation[0] * (p.rotation[4] * p.rotation[8] - p.rotation[5] * p.rotation[7])
               - p.rotation[1] * (p.rotation[3] * p.rotation[8] - p.rotation[5] * p.rotation[6])
               + p.rotation[2] * (p.rotation[3] * p.rotation[7] - p.rotation[4] * p.rotation[6]);
    EXPECT_NEAR(det, 1.0, 1e-6);

    // Translation should have a non-zero baseline
    double baseline = std::sqrt(p.translation[0] * p.translation[0]
                              + p.translation[1] * p.translation[1]
                              + p.translation[2] * p.translation[2]);
    EXPECT_GT(baseline, 0.0);
}

TEST(Calibration, SaveAndLoadRoundTrip) {
    auto original = CalibrationParams::default_sim();

    // Use a temporary file
    std::string tmp_path = std::tmpnam(nullptr);
    tmp_path += "_vxl_calib_test.json";

    // Save
    auto save_result = original.save(tmp_path);
    ASSERT_TRUE(save_result.ok()) << save_result.message;

    // Load
    auto load_result = CalibrationParams::load(tmp_path);
    ASSERT_TRUE(load_result.ok()) << load_result.message;

    const auto& loaded = load_result.value;

    // Compare all fields
    for (int i = 0; i < 9; ++i) {
        EXPECT_DOUBLE_EQ(original.camera_matrix[i], loaded.camera_matrix[i]);
        EXPECT_DOUBLE_EQ(original.projector_matrix[i], loaded.projector_matrix[i]);
        EXPECT_DOUBLE_EQ(original.rotation[i], loaded.rotation[i]);
    }
    for (int i = 0; i < 5; ++i) {
        EXPECT_DOUBLE_EQ(original.camera_distortion[i], loaded.camera_distortion[i]);
        EXPECT_DOUBLE_EQ(original.projector_distortion[i], loaded.projector_distortion[i]);
    }
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(original.translation[i], loaded.translation[i]);
    }
    EXPECT_EQ(original.image_width, loaded.image_width);
    EXPECT_EQ(original.image_height, loaded.image_height);
    EXPECT_EQ(original.projector_width, loaded.projector_width);
    EXPECT_EQ(original.projector_height, loaded.projector_height);

    // Cleanup
    std::remove(tmp_path.c_str());
}

TEST(Calibration, LoadNonexistentFileFails) {
    auto result = CalibrationParams::load("/no/such/file.json");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, ErrorCode::FILE_NOT_FOUND);
}

TEST(Calibration, SaveToInvalidPathFails) {
    auto p = CalibrationParams::default_sim();
    auto result = p.save("/no/such/directory/calib.json");
    EXPECT_FALSE(result.ok());
}

} // namespace
} // namespace vxl
