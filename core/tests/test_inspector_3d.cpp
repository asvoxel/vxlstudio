#include <cmath>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

#include "vxl/inspector_3d.h"
#include "vxl/types.h"

namespace {

// Helper: create a HeightMap filled with a constant value.
vxl::HeightMap make_flat(int w, int h, float z, float res = 0.1f) {
    auto hmap = vxl::HeightMap::create(w, h, res);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());
    for (int i = 0; i < w * h; ++i) data[i] = z;
    return hmap;
}

// Helper: create a tilted-plane HeightMap z = ax + by + c.
vxl::HeightMap make_tilted(int w, int h, float a, float b, float c,
                            float res = 0.1f) {
    auto hmap = vxl::HeightMap::create(w, h, res);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[y * w + x] = a * x + b * y + c;
        }
    }
    return hmap;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ref_plane_fit
// ---------------------------------------------------------------------------
TEST(Inspector3D, RefPlaneFit) {
    auto hmap = make_flat(100, 100, 2.0f);
    vxl::ROI roi{0, 0, 100, 100};

    auto res = vxl::Inspector3D::ref_plane_fit(hmap, roi);
    ASSERT_TRUE(res.ok());
    // Plane should pass through z=2.
    EXPECT_NEAR(res.value.distance(50, 50, 2.0), 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
// height_measure
// ---------------------------------------------------------------------------
TEST(Inspector3D, HeightMeasure_FlatWithBump) {
    auto hmap = make_flat(100, 100, 0.0f);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());

    // Add a known bump in region (40..60, 40..60) with height 0.5.
    for (int y = 40; y < 60; ++y) {
        for (int x = 40; x < 60; ++x) {
            data[y * 100 + x] = 0.5f;
        }
    }

    vxl::ROI roi{40, 40, 20, 20};
    auto res = vxl::Inspector3D::height_measure(hmap, roi, 0.0);
    ASSERT_TRUE(res.ok());

    EXPECT_NEAR(res.value.min_height, 0.5f, 1e-5f);
    EXPECT_NEAR(res.value.max_height, 0.5f, 1e-5f);
    EXPECT_NEAR(res.value.avg_height, 0.5f, 1e-5f);
    EXPECT_NEAR(res.value.std_height, 0.0f, 1e-5f);
    // Volume = 20*20 * 0.5 * pixel_area(0.1*0.1) = 200 * 0.01 = 2.0
    EXPECT_NEAR(res.value.volume, 2.0f, 0.01f);
}

TEST(Inspector3D, HeightMeasure_WithRefHeight) {
    auto hmap = make_flat(50, 50, 3.0f, 1.0f);
    vxl::ROI roi{0, 0, 50, 50};

    auto res = vxl::Inspector3D::height_measure(hmap, roi, 1.0);
    ASSERT_TRUE(res.ok());

    // Height relative to ref: 3.0 - 1.0 = 2.0
    EXPECT_NEAR(res.value.avg_height, 2.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// flatness
// ---------------------------------------------------------------------------
TEST(Inspector3D, Flatness_PerfectPlane) {
    // A perfect (tilted) plane should have flatness ~0.
    auto hmap = make_tilted(100, 100, 0.01f, 0.02f, 1.0f);
    vxl::ROI roi{0, 0, 100, 100};

    auto res = vxl::Inspector3D::flatness(hmap, roi);
    ASSERT_TRUE(res.ok());
    EXPECT_NEAR(res.value, 0.0f, 1e-3f);
}

TEST(Inspector3D, Flatness_WithDeviation) {
    auto hmap = make_flat(100, 100, 0.0f);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());

    // Add a single high point.
    data[50 * 100 + 50] = 0.3f;

    vxl::ROI roi{0, 0, 100, 100};
    auto res = vxl::Inspector3D::flatness(hmap, roi);
    ASSERT_TRUE(res.ok());

    // Flatness should be approximately 0.3 (the deviation of the high point).
    // The fit might absorb a tiny bit, but with 10000 points and one outlier
    // it should be very close.
    EXPECT_GT(res.value, 0.2f);
    EXPECT_LT(res.value, 0.35f);
}

// ---------------------------------------------------------------------------
// height_threshold
// ---------------------------------------------------------------------------
TEST(Inspector3D, HeightThreshold) {
    auto hmap = make_flat(50, 50, 0.0f);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());

    // Set a block to 0.4.
    for (int y = 10; y < 20; ++y)
        for (int x = 10; x < 20; ++x)
            data[y * 50 + x] = 0.4f;

    auto res = vxl::Inspector3D::height_threshold(hmap, 0.1f, 0.5f);
    ASSERT_TRUE(res.ok());

    const uint8_t* mask = res.value.buffer.data();
    int stride = res.value.stride;

    // Pixel inside the block (z=0.4, in range [0.1, 0.5]) should be 0 (good).
    EXPECT_EQ(mask[15 * stride + 15], 0);
    // Pixel outside the block (z=0.0, below 0.1 threshold) should be 255 (defect).
    EXPECT_EQ(mask[0 * stride + 0], 255);
}

// ---------------------------------------------------------------------------
// defect_cluster
// ---------------------------------------------------------------------------
TEST(Inspector3D, DefectCluster) {
    // Create a binary mask with two blobs.
    vxl::Image mask = vxl::Image::create(100, 100, vxl::PixelFormat::GRAY8);
    uint8_t* data = mask.buffer.data();
    std::memset(data, 0, static_cast<size_t>(mask.stride) * mask.height);

    // Blob 1: 10x10 at (20, 20).
    for (int y = 20; y < 30; ++y)
        for (int x = 20; x < 30; ++x)
            data[y * mask.stride + x] = 255;

    // Blob 2: 5x5 at (60, 60).
    for (int y = 60; y < 65; ++y)
        for (int x = 60; x < 65; ++x)
            data[y * mask.stride + x] = 255;

    float resolution_mm = 0.1f;
    auto res = vxl::Inspector3D::defect_cluster(mask, resolution_mm, 10);
    ASSERT_TRUE(res.ok());

    // Should find 2 clusters (both >= 10 pixels).
    EXPECT_EQ(res.value.size(), 2u);

    // Sort by area to get deterministic order.
    auto& regions = res.value;
    if (regions.size() == 2 && regions[0].area_mm2 < regions[1].area_mm2) {
        std::swap(regions[0], regions[1]);
    }

    // Larger blob: 100 pixels * 0.01 mm^2 = 1.0 mm^2.
    EXPECT_NEAR(regions[0].area_mm2, 1.0f, 0.01f);
    // Smaller blob: 25 pixels * 0.01 = 0.25 mm^2.
    EXPECT_NEAR(regions[1].area_mm2, 0.25f, 0.01f);
}

TEST(Inspector3D, DefectCluster_MinAreaFilter) {
    vxl::Image mask = vxl::Image::create(50, 50, vxl::PixelFormat::GRAY8);
    uint8_t* data = mask.buffer.data();
    std::memset(data, 0, static_cast<size_t>(mask.stride) * mask.height);

    // Tiny blob: 3x3 = 9 pixels.
    for (int y = 10; y < 13; ++y)
        for (int x = 10; x < 13; ++x)
            data[y * mask.stride + x] = 255;

    auto res = vxl::Inspector3D::defect_cluster(mask, 0.1f, 10);
    ASSERT_TRUE(res.ok());
    // Should be filtered out (9 < 10).
    EXPECT_EQ(res.value.size(), 0u);
}

// ---------------------------------------------------------------------------
// Inspector3D::run (orchestrator)
// ---------------------------------------------------------------------------
TEST(Inspector3D, RunOrchestrator) {
    auto hmap = make_flat(200, 200, 0.0f, 0.1f);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());

    // Add a bump.
    for (int y = 80; y < 100; ++y)
        for (int x = 80; x < 100; ++x)
            data[y * 200 + x] = 0.2f;

    vxl::Inspector3D engine;

    // Height measure inspector.
    vxl::InspectorConfig hm_cfg;
    hm_cfg.name = "bump_height";
    hm_cfg.type = "height_measure";
    hm_cfg.rois.push_back({80, 80, 20, 20});
    hm_cfg.params["min_height_mm"] = 0.1;
    hm_cfg.params["max_height_mm"] = 0.3;
    hm_cfg.severity = "critical";
    engine.add_inspector(hm_cfg);

    // Flatness inspector (warning only).
    vxl::InspectorConfig flat_cfg;
    flat_cfg.name = "board_flat";
    flat_cfg.type = "flatness";
    flat_cfg.rois.push_back({0, 0, 200, 200});
    flat_cfg.params["max_flatness_mm"] = 0.01;  // will fail (bump present)
    flat_cfg.severity = "warning";
    engine.add_inspector(flat_cfg);

    auto res = engine.run(hmap);
    ASSERT_TRUE(res.ok());

    // Overall should still be OK because flatness is only "warning".
    EXPECT_TRUE(res.value.ok);
}

TEST(Inspector3D, RunOrchestrator_CriticalFail) {
    auto hmap = make_flat(100, 100, 1.0f, 0.1f);
    vxl::Inspector3D engine;

    vxl::InspectorConfig cfg;
    cfg.name = "height_check";
    cfg.type = "height_measure";
    cfg.rois.push_back({0, 0, 100, 100});
    cfg.params["min_height_mm"] = 2.0;  // hmap avg = 1.0, which is < 2.0 -> fail
    cfg.params["max_height_mm"] = 3.0;
    cfg.severity = "critical";
    engine.add_inspector(cfg);

    auto res = engine.run(hmap);
    ASSERT_TRUE(res.ok());  // The run itself succeeds.
    EXPECT_FALSE(res.value.ok);  // But the inspection result is NG.
}

// ---------------------------------------------------------------------------
// coplanarity -- 4 ROIs on a flat plane → near-zero deviation
// ---------------------------------------------------------------------------
TEST(Inspector3D, Coplanarity_FlatPlane) {
    auto hmap = make_flat(200, 200, 1.0f, 0.1f);

    // 4 ROIs at different locations, all at same height.
    std::vector<vxl::ROI> rois = {
        {10, 10, 20, 20},
        {150, 10, 20, 20},
        {10, 150, 20, 20},
        {150, 150, 20, 20}
    };

    auto res = vxl::Inspector3D::coplanarity(hmap, rois);
    ASSERT_TRUE(res.ok());
    // All ROIs at same height => deviation should be ~0.
    EXPECT_NEAR(res.value, 0.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// coplanarity -- 4 ROIs with one elevated → deviation matches elevation
// ---------------------------------------------------------------------------
TEST(Inspector3D, Coplanarity_OneElevated) {
    auto hmap = make_flat(200, 200, 1.0f, 0.1f);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());

    // Elevate the pixels in the 4th ROI by 0.5 mm.
    for (int y = 150; y < 170; ++y)
        for (int x = 150; x < 170; ++x)
            data[y * 200 + x] = 1.5f;

    std::vector<vxl::ROI> rois = {
        {10, 10, 20, 20},
        {150, 10, 20, 20},
        {10, 150, 20, 20},
        {150, 150, 20, 20}
    };

    auto res = vxl::Inspector3D::coplanarity(hmap, rois);
    ASSERT_TRUE(res.ok());
    // The deviation should be close to the elevation (3 at 1.0, 1 at 1.5).
    // The plane will tilt slightly, so deviation won't be exactly 0.5,
    // but it should be in the right ballpark.
    EXPECT_GT(res.value, 0.1f);
    EXPECT_LT(res.value, 0.5f);
}

// ---------------------------------------------------------------------------
// coplanarity -- fewer than 3 ROIs → error
// ---------------------------------------------------------------------------
TEST(Inspector3D, Coplanarity_TooFewROIs) {
    auto hmap = make_flat(100, 100, 1.0f, 0.1f);

    std::vector<vxl::ROI> rois = {
        {10, 10, 20, 20},
        {50, 50, 20, 20}
    };

    auto res = vxl::Inspector3D::coplanarity(hmap, rois);
    EXPECT_FALSE(res.ok());
}

// ---------------------------------------------------------------------------
// template_compare -- identical height maps → 0 defects
// ---------------------------------------------------------------------------
TEST(Inspector3D, TemplateCompare_Identical) {
    auto hmap = make_flat(100, 100, 0.5f, 0.1f);
    // Use the same height map as both current and reference.
    auto res = vxl::Inspector3D::template_compare(hmap, hmap, 0.05f, 10);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.defects.size(), 0u);
    EXPECT_NEAR(res.value.max_diff, 0.0f, 1e-5f);
    EXPECT_NEAR(res.value.mean_diff, 0.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// template_compare -- height map with added bump → finds defect
// ---------------------------------------------------------------------------
TEST(Inspector3D, TemplateCompare_WithBump) {
    auto reference = make_flat(100, 100, 0.0f, 0.1f);
    auto current   = make_flat(100, 100, 0.0f, 0.1f);

    float* cur_data = reinterpret_cast<float*>(current.buffer.data());

    // Add a 15x15 bump of 0.5 mm at location (40, 40).
    for (int y = 40; y < 55; ++y)
        for (int x = 40; x < 55; ++x)
            cur_data[y * 100 + x] = 0.5f;

    auto res = vxl::Inspector3D::template_compare(current, reference, 0.1f, 10);
    ASSERT_TRUE(res.ok());

    // Should find at least one defect.
    EXPECT_GE(res.value.defects.size(), 1u);

    // Max diff should be ~0.5 (our bump height).
    EXPECT_NEAR(res.value.max_diff, 0.5f, 0.01f);

    // Verify the defect is roughly at the bump location.
    bool found_at_bump = false;
    for (const auto& d : res.value.defects) {
        if (d.bounding_box.x >= 35 && d.bounding_box.x <= 55 &&
            d.bounding_box.y >= 35 && d.bounding_box.y <= 55) {
            found_at_bump = true;
            break;
        }
    }
    EXPECT_TRUE(found_at_bump) << "Expected defect near the bump location (40,40)";
}
