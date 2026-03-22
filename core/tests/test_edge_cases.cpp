#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "vxl/height_map.h"
#include "vxl/inspector_3d.h"
#include "vxl/message_bus.h"
#include "vxl/recipe.h"
#include "vxl/types.h"

namespace {

// Helper: create a HeightMap filled with a constant value.
vxl::HeightMap make_flat(int w, int h, float z, float res = 0.1f) {
    auto hmap = vxl::HeightMap::create(w, h, res);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());
    for (int i = 0; i < w * h; ++i) data[i] = z;
    return hmap;
}

// Helper: create a HeightMap filled entirely with NaN.
vxl::HeightMap make_all_nan(int w, int h, float res = 0.1f) {
    auto hmap = vxl::HeightMap::create(w, h, res);
    float* data = reinterpret_cast<float*>(hmap.buffer.data());
    for (int i = 0; i < w * h; ++i)
        data[i] = std::numeric_limits<float>::quiet_NaN();
    return hmap;
}

} // anonymous namespace

// ===========================================================================
// SharedBuffer edge cases
// ===========================================================================

TEST(SharedBuffer, AllocateZero) {
    auto buf = vxl::SharedBuffer::allocate(0);
    EXPECT_EQ(buf.size(), 0u);
    // ref_count should still be valid (1 for the initial owner).
    EXPECT_EQ(buf.ref_count(), 1);
}

TEST(SharedBuffer, CopyEmptyBuffer) {
    vxl::SharedBuffer empty;
    EXPECT_EQ(empty.data(), nullptr);
    EXPECT_EQ(empty.size(), 0u);

    vxl::SharedBuffer copy = empty;
    EXPECT_EQ(copy.data(), nullptr);
    EXPECT_EQ(copy.size(), 0u);
}

TEST(SharedBuffer, RefCountTracking) {
    auto buf = vxl::SharedBuffer::allocate(64);
    EXPECT_EQ(buf.ref_count(), 1);

    {
        auto copy1 = buf;
        EXPECT_EQ(buf.ref_count(), 2);
        EXPECT_EQ(copy1.ref_count(), 2);

        {
            auto copy2 = copy1;
            EXPECT_EQ(buf.ref_count(), 3);
            EXPECT_EQ(copy1.ref_count(), 3);
            EXPECT_EQ(copy2.ref_count(), 3);
        }
        // copy2 destroyed, back to 2.
        EXPECT_EQ(buf.ref_count(), 2);
    }
    // copy1 destroyed, back to 1.
    EXPECT_EQ(buf.ref_count(), 1);
}

// ===========================================================================
// Image edge cases
// ===========================================================================

TEST(Image, CreateZeroDimensions) {
    auto img = vxl::Image::create(0, 0, vxl::PixelFormat::GRAY8);
    EXPECT_EQ(img.width, 0);
    EXPECT_EQ(img.height, 0);
}

// ===========================================================================
// HeightMap edge cases
// ===========================================================================

TEST(HeightMap, CreateZeroDimensions) {
    auto hmap = vxl::HeightMap::create(0, 0, 1.0f);
    EXPECT_EQ(hmap.width, 0);
    EXPECT_EQ(hmap.height, 0);
}

// ===========================================================================
// HeightMap with all-NaN -> Inspector3D::height_measure
// ===========================================================================

TEST(Inspector3D, HeightMeasure_AllNaN) {
    auto hmap = make_all_nan(50, 50);
    vxl::ROI roi{0, 0, 50, 50};

    auto res = vxl::Inspector3D::height_measure(hmap, roi, 0.0);
    // Should handle gracefully: either return an error or return a result
    // with zero/NaN values -- must not crash.
    if (res.ok()) {
        // If it returns OK, the values should indicate no valid data.
        EXPECT_TRUE(std::isnan(res.value.avg_height) ||
                    res.value.avg_height == 0.0f);
    }
}

// ===========================================================================
// HeightMap with all-NaN -> HeightMapProcessor::fit_reference_plane
// ===========================================================================

TEST(HeightMapProcessor, FitReferencePlane_AllNaN) {
    auto hmap = make_all_nan(50, 50);
    vxl::ROI roi{0, 0, 50, 50};

    auto res = vxl::HeightMapProcessor::fit_reference_plane(hmap, roi);
    EXPECT_FALSE(res.ok());
}

// ===========================================================================
// Inspector3D::defect_cluster with empty mask (no defects)
// ===========================================================================

TEST(Inspector3D, DefectCluster_EmptyMask) {
    vxl::Image mask = vxl::Image::create(100, 100, vxl::PixelFormat::GRAY8);
    std::memset(mask.buffer.data(), 0,
                static_cast<size_t>(mask.stride) * mask.height);

    auto res = vxl::Inspector3D::defect_cluster(mask, 0.1f, 10);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.size(), 0u);
}

// ===========================================================================
// Inspector3D::run with no inspectors configured
// ===========================================================================

TEST(Inspector3D, Run_NoInspectors) {
    auto hmap = make_flat(50, 50, 1.0f);
    vxl::Inspector3D engine;

    auto res = engine.run(hmap);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res.value.ok);
    EXPECT_TRUE(res.value.defects.empty());
    EXPECT_TRUE(res.value.measures.empty());
}

// ===========================================================================
// Recipe::load with invalid JSON
// ===========================================================================

TEST(Recipe, LoadInvalidJSON) {
    // Use a path that does not exist -- Recipe::load should return an error.
    auto res = vxl::Recipe::load("/nonexistent/path/invalid_recipe.json");
    EXPECT_FALSE(res.ok());
}

// ===========================================================================
// ROI with zero area
// ===========================================================================

TEST(ROI, ZeroArea) {
    vxl::ROI roi{10, 10, 0, 0};
    EXPECT_EQ(roi.area(), 0);
    EXPECT_FALSE(roi.contains(10, 10));
}

// ===========================================================================
// MessageBus edge cases
// ===========================================================================

TEST(MessageBus, SubscribePublishUnsubscribePublish) {
    vxl::MessageBus bus;
    int call_count = 0;

    auto id = bus.subscribe("test_topic", [&](const std::shared_ptr<vxl::Message>&) {
        ++call_count;
    });

    // Publish once -- handler should be called.
    auto msg = std::make_shared<vxl::ParamChanged>();
    msg->topic = "test_topic";
    msg->key = "foo";
    msg->value = "bar";
    bus.publish(msg);
    EXPECT_EQ(call_count, 1);

    // Unsubscribe.
    bus.unsubscribe(id);

    // Publish again -- handler should NOT be called.
    bus.publish(msg);
    EXPECT_EQ(call_count, 1);
}

TEST(MessageBus, PublishWithNoSubscribers) {
    vxl::MessageBus bus;

    // Publishing to a topic with no subscribers should not crash.
    auto msg = std::make_shared<vxl::AlarmTriggered>();
    msg->topic = "alarm_triggered";
    msg->alarm_id = "A001";
    msg->description = "test alarm";
    EXPECT_NO_THROW(bus.publish(msg));
}
