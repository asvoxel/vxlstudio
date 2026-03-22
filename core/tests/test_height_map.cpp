#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "vxl/height_map.h"
#include "vxl/types.h"

namespace {

// Helper: create a HeightMap filled with a constant value.
vxl::HeightMap make_flat(int w, int h, float z, float res = 1.0f) {
    auto hmap = vxl::HeightMap::create(w, h, res);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());
    for (int i = 0; i < w * h; ++i) data[i] = z;
    return hmap;
}

// Helper: create a tilted-plane HeightMap z = ax + by + c.
vxl::HeightMap make_tilted(int w, int h, float a, float b, float c,
                            float res = 1.0f) {
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
// fit_reference_plane
// ---------------------------------------------------------------------------
TEST(HeightMapProcessor, FitReferencePlane_Flat) {
    auto hmap = make_flat(100, 100, 5.0f);
    vxl::ROI roi{0, 0, 100, 100};

    auto res = vxl::HeightMapProcessor::fit_reference_plane(hmap, roi);
    ASSERT_TRUE(res.ok());

    const auto& p = res.value;
    // For z = 5 everywhere: the plane should be roughly (0, 0, -1, 5)/1 = a~0, b~0.
    // distance(x, y, 5) should be ~0 for all points.
    EXPECT_NEAR(p.distance(50.0, 50.0, 5.0), 0.0, 1e-6);
}

TEST(HeightMapProcessor, FitReferencePlane_Tilted) {
    // z = 0.1*x + 0.2*y + 3.0
    auto hmap = make_tilted(100, 100, 0.1f, 0.2f, 3.0f);
    vxl::ROI roi{0, 0, 100, 100};

    auto res = vxl::HeightMapProcessor::fit_reference_plane(hmap, roi);
    ASSERT_TRUE(res.ok());

    // Every point should have ~0 distance to the fitted plane.
    for (int y = 10; y < 90; y += 20) {
        for (int x = 10; x < 90; x += 20) {
            double z = 0.1 * x + 0.2 * y + 3.0;
            EXPECT_NEAR(res.value.distance(x, y, z), 0.0, 1e-4);
        }
    }
}

TEST(HeightMapProcessor, FitReferencePlane_TooFewPixels) {
    auto hmap = vxl::HeightMap::create(10, 10, 1.0f);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());
    for (int i = 0; i < 100; ++i)
        data[i] = std::numeric_limits<float>::quiet_NaN();

    vxl::ROI roi{0, 0, 10, 10};
    auto res = vxl::HeightMapProcessor::fit_reference_plane(hmap, roi);
    EXPECT_FALSE(res.ok());
}

// ---------------------------------------------------------------------------
// subtract_reference
// ---------------------------------------------------------------------------
TEST(HeightMapProcessor, SubtractReference) {
    auto hmap = make_tilted(50, 50, 0.1f, 0.2f, 3.0f);
    vxl::ROI roi{0, 0, 50, 50};

    auto plane_res = vxl::HeightMapProcessor::fit_reference_plane(hmap, roi);
    ASSERT_TRUE(plane_res.ok());

    auto sub_res = vxl::HeightMapProcessor::subtract_reference(hmap, plane_res.value);
    ASSERT_TRUE(sub_res.ok());

    // After subtraction, all values should be ~0.
    // subtract_reference computes z - z_plane where z_plane is the plane's
    // z-value at each (x,y).  Floating-point accumulation in the Cramer's-rule
    // plane fit can produce residuals up to ~1e-4.
    const float* data = reinterpret_cast<const float*>(sub_res.value.buffer.data());
    for (int i = 0; i < 50 * 50; ++i) {
        EXPECT_NEAR(data[i], 0.0f, 1e-3f);
    }
}

// ---------------------------------------------------------------------------
// apply_filter
// ---------------------------------------------------------------------------
TEST(HeightMapProcessor, ApplyFilter_Median) {
    auto hmap = make_flat(50, 50, 1.0f);
    auto res = vxl::HeightMapProcessor::apply_filter(hmap, "median", 3);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.width, 50);
    EXPECT_EQ(res.value.height, 50);
}

TEST(HeightMapProcessor, ApplyFilter_Gaussian) {
    auto hmap = make_flat(50, 50, 2.0f);
    auto res = vxl::HeightMapProcessor::apply_filter(hmap, "gaussian", 5);
    ASSERT_TRUE(res.ok());

    // For a constant image, gaussian output should still be the same constant.
    const float* data = reinterpret_cast<const float*>(res.value.buffer.data());
    EXPECT_NEAR(data[25 * 50 + 25], 2.0f, 1e-4f);
}

TEST(HeightMapProcessor, ApplyFilter_InvalidType) {
    auto hmap = make_flat(10, 10, 1.0f);
    auto res = vxl::HeightMapProcessor::apply_filter(hmap, "bilateral", 3);
    EXPECT_FALSE(res.ok());
}

// ---------------------------------------------------------------------------
// crop_roi
// ---------------------------------------------------------------------------
TEST(HeightMapProcessor, CropROI) {
    auto hmap = make_tilted(100, 100, 1.0f, 0.0f, 0.0f);
    vxl::ROI roi{10, 20, 30, 40};

    auto cropped = vxl::HeightMapProcessor::crop_roi(hmap, roi);
    EXPECT_EQ(cropped.width, 30);
    EXPECT_EQ(cropped.height, 40);

    const float* data = reinterpret_cast<const float*>(cropped.buffer.data());
    // Top-left pixel should correspond to (10, 20) -> z = 1.0*10 = 10.0
    EXPECT_NEAR(data[0], 10.0f, 1e-5f);
}

TEST(HeightMapProcessor, CropROI_Clamped) {
    auto hmap = make_flat(50, 50, 1.0f);
    vxl::ROI roi{40, 40, 100, 100};  // extends beyond image

    auto cropped = vxl::HeightMapProcessor::crop_roi(hmap, roi);
    EXPECT_EQ(cropped.width, 10);
    EXPECT_EQ(cropped.height, 10);
}

// ---------------------------------------------------------------------------
// interpolate_holes
// ---------------------------------------------------------------------------
TEST(HeightMapProcessor, InterpolateHoles) {
    auto hmap = make_flat(20, 20, 7.0f);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());
    // Punch a hole.
    data[10 * 20 + 10] = std::numeric_limits<float>::quiet_NaN();
    data[10 * 20 + 11] = std::numeric_limits<float>::quiet_NaN();

    auto res = vxl::HeightMapProcessor::interpolate_holes(hmap);
    ASSERT_TRUE(res.ok());

    const float* out = reinterpret_cast<const float*>(res.value.buffer.data());
    // Holes should be filled with nearest valid value (7.0).
    EXPECT_NEAR(out[10 * 20 + 10], 7.0f, 1e-5f);
    EXPECT_NEAR(out[10 * 20 + 11], 7.0f, 1e-5f);
}
