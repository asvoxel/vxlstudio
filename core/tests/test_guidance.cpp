// VxlStudio tests -- GuidanceEngine
// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstring>

#include <gtest/gtest.h>

#include "vxl/guidance.h"
#include "vxl/types.h"

namespace {

// Helper: create a HeightMap filled with zeros, then poke peaks at given coords.
vxl::HeightMap make_hmap_with_peaks(int W, int H, float resolution,
                                     const std::vector<std::tuple<int, int, float>>& peaks) {
    auto hmap = vxl::HeightMap::create(W, H, resolution);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());

    // Fill with base height = 0
    std::memset(data, 0, static_cast<size_t>(W) * H * sizeof(float));

    // Set peaks (and spread a small region around each for flatness)
    for (const auto& [px, py, pz] : peaks) {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                int nx = px + dx;
                int ny = py + dy;
                if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                    float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                    float falloff = std::max(0.0f, 1.0f - dist / 3.0f);
                    float& val = data[ny * W + nx];
                    val = std::max(val, pz * falloff);
                }
            }
        }
        // Ensure the exact peak pixel has the full value
        data[py * W + px] = pz;
    }

    return hmap;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// find_pick_point
// ---------------------------------------------------------------------------
TEST(GuidanceEngine, FindPickPoint_KnownPeak) {
    // Create a 100x100 height map with a single peak at (50, 50) = 42.0
    auto hmap = make_hmap_with_peaks(100, 100, 1.0f, {{50, 50, 42.0f}});

    vxl::ROI roi{0, 0, 100, 100};
    auto result = vxl::GuidanceEngine::find_pick_point(hmap, roi);
    ASSERT_TRUE(result.ok());

    // The pick point translation should correspond to the peak location
    EXPECT_NEAR(result.value.pose.translation[0], 50.0, 1.0);
    EXPECT_NEAR(result.value.pose.translation[1], 50.0, 1.0);
    EXPECT_NEAR(result.value.pose.translation[2], 42.0, 0.01);
    EXPECT_FLOAT_EQ(result.value.score, 1.0f);
}

TEST(GuidanceEngine, FindPickPoint_RoiSubset) {
    // Two peaks: (20,20)=10 and (80,80)=50. ROI covers only the first.
    auto hmap = make_hmap_with_peaks(100, 100, 1.0f, {{20, 20, 10.0f}, {80, 80, 50.0f}});

    vxl::ROI roi{0, 0, 40, 40};
    auto result = vxl::GuidanceEngine::find_pick_point(hmap, roi);
    ASSERT_TRUE(result.ok());

    // Should find the peak at (20,20)=10, not the higher one outside the ROI
    EXPECT_NEAR(result.value.pose.translation[2], 10.0, 0.01);
}

// ---------------------------------------------------------------------------
// camera_to_robot with identity transform
// ---------------------------------------------------------------------------
TEST(GuidanceEngine, CameraToRobot_Identity) {
    vxl::Pose6D camera_pose;
    camera_pose.translation[0] = 10.0;
    camera_pose.translation[1] = 20.0;
    camera_pose.translation[2] = 30.0;
    // rotation = identity (default)

    vxl::Pose6D identity;  // default is identity rotation, zero translation

    auto result = vxl::GuidanceEngine::camera_to_robot(camera_pose, identity);
    ASSERT_TRUE(result.ok());

    // T_robot = I * T_camera => same pose
    EXPECT_NEAR(result.value.translation[0], 10.0, 1e-9);
    EXPECT_NEAR(result.value.translation[1], 20.0, 1e-9);
    EXPECT_NEAR(result.value.translation[2], 30.0, 1e-9);

    // Rotation should remain identity
    EXPECT_NEAR(result.value.rotation[0], 1.0, 1e-9);
    EXPECT_NEAR(result.value.rotation[4], 1.0, 1e-9);
    EXPECT_NEAR(result.value.rotation[8], 1.0, 1e-9);
    EXPECT_NEAR(result.value.rotation[1], 0.0, 1e-9);
}

TEST(GuidanceEngine, CameraToRobot_WithTranslation) {
    vxl::Pose6D camera_pose;
    camera_pose.translation[0] = 5.0;
    camera_pose.translation[1] = 0.0;
    camera_pose.translation[2] = 0.0;

    vxl::Pose6D hand_eye;
    // identity rotation, but with offset translation
    hand_eye.translation[0] = 100.0;
    hand_eye.translation[1] = 200.0;
    hand_eye.translation[2] = 300.0;

    auto result = vxl::GuidanceEngine::camera_to_robot(camera_pose, hand_eye);
    ASSERT_TRUE(result.ok());

    // t_result = R_he * t_cam + t_he = I * (5,0,0) + (100,200,300) = (105,200,300)
    EXPECT_NEAR(result.value.translation[0], 105.0, 1e-9);
    EXPECT_NEAR(result.value.translation[1], 200.0, 1e-9);
    EXPECT_NEAR(result.value.translation[2], 300.0, 1e-9);
}

// ---------------------------------------------------------------------------
// compute_grasp_2d on HeightMap with distinct peaks
// ---------------------------------------------------------------------------
TEST(GuidanceEngine, ComputeGrasp2D_MultiplePeaks) {
    // Create 200x200 hmap with 3 peaks of different heights, spaced far apart.
    auto hmap = make_hmap_with_peaks(200, 200, 1.0f,
        {{30, 30, 80.0f}, {100, 100, 120.0f}, {170, 170, 60.0f}});

    vxl::GuidanceEngine engine;
    vxl::GuidanceParams params;
    params.strategy = "top_down";
    params.min_score = 0.0f;
    params.max_results = 10;
    params.approach_distance_mm = 0.0f;  // no approach offset for testing

    auto result = engine.compute_grasp_2d(hmap, params);
    ASSERT_TRUE(result.ok());

    // Should find at least 1 grasp pose
    ASSERT_GE(result.value.size(), 1u);

    // Results should be sorted by score descending
    for (size_t i = 1; i < result.value.size(); ++i) {
        EXPECT_GE(result.value[i - 1].score, result.value[i].score);
    }

    // The highest grasp z should be one of our peak heights (80, 120, or 60)
    float top_z = static_cast<float>(result.value[0].pose.translation[2]);
    bool valid_peak = (std::abs(top_z - 80.0f) < 5.0f) ||
                      (std::abs(top_z - 120.0f) < 5.0f) ||
                      (std::abs(top_z - 60.0f) < 5.0f);
    EXPECT_TRUE(valid_peak) << "Top grasp z=" << top_z << " should be near a known peak";
}

TEST(GuidanceEngine, ComputeGrasp2D_EmptyMap) {
    vxl::HeightMap empty;
    vxl::GuidanceEngine engine;

    auto result = engine.compute_grasp_2d(empty);
    EXPECT_FALSE(result.ok());
}
