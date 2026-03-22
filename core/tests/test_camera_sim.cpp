#include <gtest/gtest.h>

#include "vxl/camera.h"

namespace vxl {
namespace {

TEST(SimCamera, EnumerateIncludesSim001) {
    auto devices = Camera::enumerate();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(devices[0], "SIM-001");
}

TEST(SimCamera, Open3dSucceeds) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.value, nullptr);
    EXPECT_TRUE(result.value->is_open());
    EXPECT_EQ(result.value->device_id(), "SIM-001");
}

TEST(SimCamera, Open3dUnknownDeviceFails) {
    auto result = Camera::open_3d("NONEXISTENT");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, ErrorCode::DEVICE_NOT_FOUND);
}

TEST(SimCamera, CloseAndReopen) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    auto& cam = result.value;

    EXPECT_TRUE(cam->is_open());
    cam->close();
    EXPECT_FALSE(cam->is_open());

    auto r2 = cam->open();
    EXPECT_TRUE(r2.ok());
    EXPECT_TRUE(cam->is_open());
}

TEST(SimCamera, CaptureSequenceReturnsCorrectFrameCount) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    auto& cam = result.value;

    // Default fringe count is 12
    EXPECT_EQ(cam->fringe_count(), 12);

    auto seq = cam->capture_sequence();
    ASSERT_TRUE(seq.ok());
    EXPECT_EQ(static_cast<int>(seq.value.size()), 12);
}

TEST(SimCamera, FrameDimensionsMatch) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    auto& cam = result.value;

    auto seq = cam->capture_sequence();
    ASSERT_TRUE(seq.ok());

    for (const auto& frame : seq.value) {
        EXPECT_EQ(frame.width, 1280);
        EXPECT_EQ(frame.height, 1024);
        EXPECT_EQ(frame.format, PixelFormat::GRAY8);
        EXPECT_GE(frame.stride, frame.width);
    }
}

TEST(SimCamera, PixelValuesInValidRange) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    auto& cam = result.value;

    auto seq = cam->capture_sequence();
    ASSERT_TRUE(seq.ok());

    for (const auto& frame : seq.value) {
        const uint8_t* data = frame.buffer.data();
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                uint8_t val = data[y * frame.stride + x];
                // GRAY8 is always 0-255 by type, but verify data exists
                // and is not all zeros (would indicate broken generation)
                (void)val;
            }
        }
        // Verify not all-zero: check a few pixels from the center
        int cx = frame.width / 2;
        int cy = frame.height / 2;
        uint8_t center = data[cy * frame.stride + cx];
        // With I0=128 and Im=90, center pixel should be non-zero
        EXPECT_GT(center, 0);
    }
}

TEST(SimCamera, SetFringeCountChangesSequenceLength) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    auto& cam = result.value;

    auto r = cam->set_fringe_count(8);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(cam->fringe_count(), 8);

    auto seq = cam->capture_sequence();
    ASSERT_TRUE(seq.ok());
    EXPECT_EQ(static_cast<int>(seq.value.size()), 8);
}

TEST(SimCamera, SetExposure) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    auto& cam = result.value;

    auto r = cam->set_exposure(10000);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(cam->exposure(), 10000);

    auto r2 = cam->set_exposure(-1);
    EXPECT_FALSE(r2.ok());
}

TEST(SimCamera, CaptureWhenClosedFails) {
    auto result = Camera::open_3d("SIM-001");
    ASSERT_TRUE(result.ok());
    auto& cam = result.value;

    cam->close();
    auto seq = cam->capture_sequence();
    EXPECT_FALSE(seq.ok());
}

} // namespace
} // namespace vxl
