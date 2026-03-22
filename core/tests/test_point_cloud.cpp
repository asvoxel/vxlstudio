#include <cmath>

#include <gtest/gtest.h>

#include "vxl/point_cloud.h"
#include "vxl/types.h"

namespace {

// Helper: create a PointCloud with the given XYZ points (3 floats per point).
vxl::PointCloud make_cloud(const std::vector<std::array<float, 3>>& pts) {
    vxl::PointCloud cloud;
    cloud.format = vxl::PointFormat::XYZ_FLOAT;
    cloud.point_count = pts.size();

    constexpr size_t stride = 3 * sizeof(float);
    cloud.buffer = vxl::SharedBuffer::allocate(pts.size() * stride);

    float* dst = reinterpret_cast<float*>(cloud.buffer.data());
    for (size_t i = 0; i < pts.size(); ++i) {
        dst[i * 3 + 0] = pts[i][0];
        dst[i * 3 + 1] = pts[i][1];
        dst[i * 3 + 2] = pts[i][2];
    }
    return cloud;
}

// Helper: create an empty PointCloud.
vxl::PointCloud make_empty_cloud() {
    vxl::PointCloud cloud;
    cloud.format = vxl::PointFormat::XYZ_FLOAT;
    cloud.point_count = 0;
    return cloud;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// filter_statistical
// ---------------------------------------------------------------------------
TEST(PointCloudOps, FilterStatistical_RemovesOutlier) {
    // Create a tight cluster of points near the origin, plus one far-away outlier.
    std::vector<std::array<float, 3>> pts;
    for (int i = 0; i < 50; ++i) {
        float x = static_cast<float>(i % 10) * 0.1f;
        float y = static_cast<float>(i / 10) * 0.1f;
        pts.push_back({x, y, 0.0f});
    }
    // Add a distant outlier.
    pts.push_back({100.0f, 100.0f, 100.0f});

    auto cloud = make_cloud(pts);
    ASSERT_EQ(cloud.point_count, 51u);

    auto res = vxl::PointCloudOps::filter_statistical(cloud, 10, 2.0);
    ASSERT_TRUE(res.ok());

    // The outlier should be removed; the 50 cluster points should remain.
    EXPECT_LT(res.value.point_count, cloud.point_count);
    EXPECT_GE(res.value.point_count, 49u);  // at least the cluster survives
}

TEST(PointCloudOps, FilterStatistical_EmptyCloud) {
    auto cloud = make_empty_cloud();

    auto res = vxl::PointCloudOps::filter_statistical(cloud, 10, 2.0);
    // Should succeed (or fail gracefully) but not crash.
    if (res.ok()) {
        EXPECT_EQ(res.value.point_count, 0u);
    }
    // If it returns an error, that's also acceptable -- just no crash.
}

// ---------------------------------------------------------------------------
// downsample_voxel
// ---------------------------------------------------------------------------
TEST(PointCloudOps, DownsampleVoxel_ReducesCount) {
    // Create a 10x10x1 grid of points with spacing 0.1.
    std::vector<std::array<float, 3>> pts;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            pts.push_back({x * 0.1f, y * 0.1f, 0.0f});
        }
    }
    auto cloud = make_cloud(pts);
    ASSERT_EQ(cloud.point_count, 100u);

    // Voxel size = 0.5: each voxel covers 5x5 points -> should collapse to ~4 points.
    auto res = vxl::PointCloudOps::downsample_voxel(cloud, 0.5);
    ASSERT_TRUE(res.ok());

    EXPECT_LT(res.value.point_count, cloud.point_count);
    EXPECT_GT(res.value.point_count, 0u);
}

TEST(PointCloudOps, DownsampleVoxel_EmptyCloud) {
    auto cloud = make_empty_cloud();

    auto res = vxl::PointCloudOps::downsample_voxel(cloud, 1.0);
    // Should succeed with zero points or fail gracefully.
    if (res.ok()) {
        EXPECT_EQ(res.value.point_count, 0u);
    }
}
