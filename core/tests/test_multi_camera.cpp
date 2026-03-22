#include <gtest/gtest.h>

#include "vxl/camera_manager.h"
#include "vxl/calibration.h"
#include "vxl/types.h"

// ---------------------------------------------------------------------------
// CameraManager: add two sim cameras, capture_all returns data from both
// ---------------------------------------------------------------------------
TEST(CameraManager, AddTwoCameras_CaptureAll) {
    vxl::CameraManager mgr;

    vxl::CalibrationParams calib = vxl::CalibrationParams::default_sim();

    auto r1 = mgr.add_camera("SIM-001", calib);
    ASSERT_TRUE(r1.ok()) << r1.message;

    auto r2 = mgr.add_camera("SIM-002", calib);
    ASSERT_TRUE(r2.ok()) << r2.message;

    // Verify ids.
    auto ids = mgr.camera_ids();
    ASSERT_EQ(ids.size(), 2u);

    // Verify cameras are accessible.
    EXPECT_NE(mgr.get_camera("SIM-001"), nullptr);
    EXPECT_NE(mgr.get_camera("SIM-002"), nullptr);
    EXPECT_EQ(mgr.get_camera("SIM-999"), nullptr);

    // Capture from all.
    auto cap_res = mgr.capture_all();
    ASSERT_TRUE(cap_res.ok()) << cap_res.message;

    const auto& captures = cap_res.value;
    EXPECT_EQ(captures.size(), 2u);
    EXPECT_TRUE(captures.count("SIM-001"));
    EXPECT_TRUE(captures.count("SIM-002"));

    // Each camera should return 12 frames (default fringe_count = 12).
    for (const auto& [id, frames] : captures) {
        EXPECT_EQ(frames.size(), 12u) << "Camera " << id;
    }
}

// ---------------------------------------------------------------------------
// CameraManager: duplicate add should fail
// ---------------------------------------------------------------------------
TEST(CameraManager, DuplicateAdd_Fails) {
    vxl::CameraManager mgr;
    vxl::CalibrationParams calib = vxl::CalibrationParams::default_sim();

    auto r1 = mgr.add_camera("SIM-001", calib);
    ASSERT_TRUE(r1.ok());

    auto r2 = mgr.add_camera("SIM-001", calib);
    EXPECT_FALSE(r2.ok());
}

// ---------------------------------------------------------------------------
// CameraManager: remove camera
// ---------------------------------------------------------------------------
TEST(CameraManager, RemoveCamera) {
    vxl::CameraManager mgr;
    vxl::CalibrationParams calib = vxl::CalibrationParams::default_sim();

    mgr.add_camera("SIM-001", calib);
    mgr.add_camera("SIM-002", calib);

    auto r = mgr.remove_camera("SIM-001");
    ASSERT_TRUE(r.ok());

    EXPECT_EQ(mgr.camera_ids().size(), 1u);
    EXPECT_EQ(mgr.get_camera("SIM-001"), nullptr);

    // Removing nonexistent should fail.
    auto r2 = mgr.remove_camera("SIM-999");
    EXPECT_FALSE(r2.ok());
}

// ---------------------------------------------------------------------------
// aggregate_results: all OK → overall OK
// ---------------------------------------------------------------------------
TEST(CameraManager, AggregateResults_AllOK) {
    vxl::InspectionResult ir1;
    ir1.ok = true;
    ir1.timestamp = "2026-01-01T00:00:00Z";

    vxl::InspectionResult ir2;
    ir2.ok = true;
    ir2.timestamp = "2026-01-01T00:00:01Z";

    std::vector<std::pair<std::string, vxl::InspectionResult>> per_cam = {
        {"SIM-001", ir1},
        {"SIM-002", ir2}
    };

    auto combined = vxl::CameraManager::aggregate_results(per_cam);
    EXPECT_TRUE(combined.ok);
    EXPECT_EQ(combined.defects.size(), 0u);
}

// ---------------------------------------------------------------------------
// aggregate_results: one NG → overall NG, defects merged
// ---------------------------------------------------------------------------
TEST(CameraManager, AggregateResults_OneNG) {
    vxl::InspectionResult ir1;
    ir1.ok = true;
    ir1.timestamp = "2026-01-01T00:00:00Z";

    vxl::InspectionResult ir2;
    ir2.ok = false;
    ir2.timestamp = "2026-01-01T00:00:01Z";

    vxl::DefectRegion defect;
    defect.type = "scratch";
    defect.area_mm2 = 1.5f;
    ir2.defects.push_back(defect);

    std::vector<std::pair<std::string, vxl::InspectionResult>> per_cam = {
        {"SIM-001", ir1},
        {"SIM-002", ir2}
    };

    auto combined = vxl::CameraManager::aggregate_results(per_cam);
    EXPECT_FALSE(combined.ok);
    ASSERT_EQ(combined.defects.size(), 1u);
    // Defect type should be prefixed with camera id.
    EXPECT_EQ(combined.defects[0].type, "SIM-002/scratch");
}
