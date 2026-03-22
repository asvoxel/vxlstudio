#include <cmath>
#include <cstring>

#include <gtest/gtest.h>

#include "vxl/inspector_2d.h"
#include "vxl/types.h"

namespace {

// Helper: create a GRAY8 image filled with a constant value.
vxl::Image make_gray(int w, int h, uint8_t val) {
    auto img = vxl::Image::create(w, h, vxl::PixelFormat::GRAY8);
    std::memset(img.buffer.data(), val,
                static_cast<size_t>(img.stride) * img.height);
    return img;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// template_match
// ---------------------------------------------------------------------------
TEST(Inspector2D, TemplateMatch_BasicSquare) {
    // Create 200x200 gray image with a 30x30 gradient patch at (50, 50).
    auto image = make_gray(200, 200, 128);
    uint8_t* img_data = image.buffer.data();

    for (int y = 50; y < 80; ++y)
        for (int x = 50; x < 80; ++x)
            img_data[y * image.stride + x] = static_cast<uint8_t>(x - 50 + y - 50);

    // Create matching 30x30 gradient template.
    auto templ = make_gray(30, 30, 0);
    uint8_t* tpl_data = templ.buffer.data();
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x)
            tpl_data[y * templ.stride + x] = static_cast<uint8_t>(x + y);

    auto res = vxl::Inspector2D::template_match(image, templ, 0.5f);
    ASSERT_TRUE(res.ok());

    const auto& mr = res.value;
    EXPECT_TRUE(mr.found);
    EXPECT_GT(mr.score, 0.9f);
    // Match position should be near (50, 50).
    EXPECT_NEAR(mr.x, 50.0f, 3.0f);
    EXPECT_NEAR(mr.y, 50.0f, 3.0f);
}

// ---------------------------------------------------------------------------
// blob_analysis: 3 white blobs on black background
// ---------------------------------------------------------------------------
TEST(Inspector2D, BlobAnalysis_ThreeBlobs) {
    auto image = make_gray(200, 200, 0);
    uint8_t* data = image.buffer.data();

    // Blob 1: 20x20 at (10, 10) -> area ~400
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 30; ++x)
            data[y * image.stride + x] = 255;

    // Blob 2: 15x15 at (60, 60) -> area ~225
    for (int y = 60; y < 75; ++y)
        for (int x = 60; x < 75; ++x)
            data[y * image.stride + x] = 255;

    // Blob 3: 10x10 at (120, 120) -> area ~100
    for (int y = 120; y < 130; ++y)
        for (int x = 120; x < 130; ++x)
            data[y * image.stride + x] = 255;

    auto res = vxl::Inspector2D::blob_analysis(image, 128, 10, 100000);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.size(), 3u);

    // Verify approximate areas (sorted by area descending for determinism).
    auto blobs = res.value;
    std::sort(blobs.begin(), blobs.end(),
              [](const vxl::BlobResult& a, const vxl::BlobResult& b) {
                  return a.area > b.area;
              });

    EXPECT_NEAR(blobs[0].area, 400.0f, 50.0f);  // contour area ≈ (n-1)²
    EXPECT_NEAR(blobs[1].area, 225.0f, 40.0f);
    EXPECT_NEAR(blobs[2].area, 100.0f, 25.0f);
}

// ---------------------------------------------------------------------------
// blob_analysis: empty image -> 0 blobs
// ---------------------------------------------------------------------------
TEST(Inspector2D, BlobAnalysis_EmptyImage) {
    auto image = make_gray(100, 100, 0);  // all black

    auto res = vxl::Inspector2D::blob_analysis(image, 128, 10, 100000);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.size(), 0u);
}

// ---------------------------------------------------------------------------
// edge_detect: half white / half black should produce edges near boundary
// ---------------------------------------------------------------------------
TEST(Inspector2D, EdgeDetect_SharpEdge) {
    auto image = make_gray(100, 100, 0);
    uint8_t* data = image.buffer.data();

    // Left half black (already 0), right half white.
    for (int y = 0; y < 100; ++y)
        for (int x = 50; x < 100; ++x)
            data[y * image.stride + x] = 255;

    auto res = vxl::Inspector2D::edge_detect(image, 50.0, 150.0);
    ASSERT_TRUE(res.ok());

    const auto& edge_img = res.value;
    EXPECT_EQ(edge_img.format, vxl::PixelFormat::GRAY8);
    EXPECT_EQ(edge_img.width, 100);
    EXPECT_EQ(edge_img.height, 100);

    // Count non-zero pixels near the vertical edge (column ~50).
    const uint8_t* edge_data = edge_img.buffer.data();
    int edge_count = 0;
    for (int y = 5; y < 95; ++y) {
        for (int x = 45; x < 55; ++x) {
            if (edge_data[y * edge_img.stride + x] > 0)
                ++edge_count;
        }
    }
    // Should have a significant number of edge pixels near the boundary.
    EXPECT_GT(edge_count, 20);
}

// ---------------------------------------------------------------------------
// template_match: empty image returns error
// ---------------------------------------------------------------------------
TEST(Inspector2D, TemplateMatch_EmptyImageReturnsError) {
    vxl::Image empty;
    auto templ = make_gray(10, 10, 128);

    auto res = vxl::Inspector2D::template_match(empty, templ, 0.5f);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// template_match: template larger than image returns error
// ---------------------------------------------------------------------------
TEST(Inspector2D, TemplateMatch_TemplateTooLargeReturnsError) {
    auto image = make_gray(20, 20, 128);
    auto templ = make_gray(30, 30, 128);

    auto res = vxl::Inspector2D::template_match(image, templ, 0.5f);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// template_match: no good match results in found==false
// ---------------------------------------------------------------------------
TEST(Inspector2D, TemplateMatch_NoMatchMeansNotFound) {
    // Image: all zeros.  Template: strong gradient that won't match.
    auto image = make_gray(100, 100, 0);
    auto templ = make_gray(20, 20, 0);
    uint8_t* tpl_data = templ.buffer.data();
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            tpl_data[y * templ.stride + x] = static_cast<uint8_t>(x * 12);

    auto res = vxl::Inspector2D::template_match(image, templ, 0.99f);
    ASSERT_TRUE(res.ok());
    EXPECT_FALSE(res.value.found);
}

// ---------------------------------------------------------------------------
// blob_analysis: min_area / max_area filtering
// ---------------------------------------------------------------------------
TEST(Inspector2D, BlobAnalysis_AreaFiltering) {
    auto image = make_gray(200, 200, 0);
    uint8_t* data = image.buffer.data();

    // Small blob: 5x5 = area ~25
    for (int y = 10; y < 15; ++y)
        for (int x = 10; x < 15; ++x)
            data[y * image.stride + x] = 255;

    // Large blob: 40x40 = area ~1600
    for (int y = 60; y < 100; ++y)
        for (int x = 60; x < 100; ++x)
            data[y * image.stride + x] = 255;

    // Filter: only keep blobs with area 50..500 -> neither should pass
    auto res = vxl::Inspector2D::blob_analysis(image, 128, 50, 500);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.size(), 0u);

    // Filter: only keep blobs with area 10..100 -> only the small blob
    auto res2 = vxl::Inspector2D::blob_analysis(image, 128, 10, 100);
    ASSERT_TRUE(res2.ok());
    EXPECT_EQ(res2.value.size(), 1u);
}

// ---------------------------------------------------------------------------
// blob_analysis: circularity is computed (rectangle should be < 1)
// ---------------------------------------------------------------------------
TEST(Inspector2D, BlobAnalysis_CircularityCheck) {
    auto image = make_gray(200, 200, 0);
    uint8_t* data = image.buffer.data();

    // Elongated rectangle: 50x10 -> low circularity
    for (int y = 80; y < 90; ++y)
        for (int x = 20; x < 70; ++x)
            data[y * image.stride + x] = 255;

    auto res = vxl::Inspector2D::blob_analysis(image, 128, 10, 100000);
    ASSERT_TRUE(res.ok());
    ASSERT_EQ(res.value.size(), 1u);

    // A thin rectangle has circularity well below 1.0
    EXPECT_GT(res.value[0].circularity, 0.0f);
    EXPECT_LT(res.value[0].circularity, 0.8f);

    // Centroid should be approximately at the center of the rectangle
    EXPECT_NEAR(res.value[0].centroid.x, 44.5f, 3.0f);
    EXPECT_NEAR(res.value[0].centroid.y, 84.5f, 3.0f);
}

// ---------------------------------------------------------------------------
// edge_detect: uniform image should produce no edges
// ---------------------------------------------------------------------------
TEST(Inspector2D, EdgeDetect_UniformImageNoEdges) {
    auto image = make_gray(100, 100, 128);  // uniform gray

    auto res = vxl::Inspector2D::edge_detect(image, 50.0, 150.0);
    ASSERT_TRUE(res.ok());

    // Count non-zero pixels -- should be zero or near-zero
    const uint8_t* edge_data = res.value.buffer.data();
    int edge_count = 0;
    for (int y = 0; y < res.value.height; ++y)
        for (int x = 0; x < res.value.width; ++x)
            if (edge_data[y * res.value.stride + x] > 0)
                ++edge_count;

    EXPECT_EQ(edge_count, 0);
}

// ---------------------------------------------------------------------------
// edge_detect: empty image returns error
// ---------------------------------------------------------------------------
TEST(Inspector2D, EdgeDetect_EmptyImageReturnsError) {
    vxl::Image empty;
    auto res = vxl::Inspector2D::edge_detect(empty, 50.0, 150.0);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// ocr: null model returns MODEL_LOAD_FAILED
// ---------------------------------------------------------------------------
TEST(Inspector2D, Ocr_NullModelReturnsError) {
    auto image = make_gray(100, 100, 128);
    vxl::ROI roi{0, 0, 100, 100};

    auto res = vxl::Inspector2D::ocr(image, roi, nullptr);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::MODEL_LOAD_FAILED);
}

// ---------------------------------------------------------------------------
// anomaly_detect: null model returns MODEL_LOAD_FAILED
// ---------------------------------------------------------------------------
TEST(Inspector2D, AnomalyDetect_NullModelReturnsError) {
    auto image = make_gray(100, 100, 128);

    auto res = vxl::Inspector2D::anomaly_detect(image, nullptr, 0.5f);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::MODEL_LOAD_FAILED);
}
