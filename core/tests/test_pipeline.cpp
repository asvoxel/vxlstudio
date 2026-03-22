// Tests for the Pipeline linear executor.
//
// Covers:
//   1. Build pipeline programmatically, run_once with SimCamera
//   2. Load pipeline from JSON, verify steps
//   3. Save / load round-trip
//   4. start() / stop() continuous mode
//   5. Invalid step type produces error
//   6. Custom step callback execution

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "vxl/calibration.h"
#include "vxl/pipeline.h"
#include "vxl/types.h"

namespace {

// Path to the test recipe shipped with the project.
static std::string project_recipe_path() {
    return std::string(VXL_TEST_DATA_DIR) + "/pcb_smt/pcb_model_a.json";
}

// Path to the pipeline JSON shipped with the project.
static std::string project_pipeline_path() {
    return std::string(VXL_TEST_DATA_DIR) + "/pipeline_pcb.json";
}

static const char* kTmpPipelinePath = "/tmp/vxl_test_pipeline_roundtrip.json";

// ---------------------------------------------------------------------------
// Test 1: Build programmatically and run_once
// ---------------------------------------------------------------------------
TEST(Pipeline, ProgrammaticBuildAndRunOnce) {
    vxl::Pipeline pipe;
    pipe.set_camera_id("SIM-001");
    pipe.set_calibration(vxl::CalibrationParams::default_sim());
    pipe.set_recipe(project_recipe_path());

    // Step 1: capture
    vxl::PipelineStep capture;
    capture.type = vxl::StepType::CAPTURE;
    capture.name = "grab";
    pipe.add_step(capture);

    // Step 2: reconstruct
    vxl::PipelineStep recon;
    recon.type = vxl::StepType::RECONSTRUCT;
    recon.name = "build_3d";
    recon.params["method"] = "structured_light";
    pipe.add_step(recon);

    // Step 3: inspect_3d
    vxl::PipelineStep insp;
    insp.type = vxl::StepType::INSPECT_3D;
    insp.name = "check_3d";
    pipe.add_step(insp);

    ASSERT_EQ(pipe.steps().size(), 3u);

    auto res = pipe.run_once();
    ASSERT_TRUE(res.ok()) << res.message;

    // Context should have frames from capture
    EXPECT_FALSE(res.value.frames.empty());

    // Height map should be populated
    EXPECT_GT(res.value.height_map.width, 0);
    EXPECT_GT(res.value.height_map.height, 0);

    // Point cloud should have points
    EXPECT_GT(res.value.point_cloud.point_count, 0u);

    // Inspection result should have a recipe name
    EXPECT_FALSE(res.value.result.recipe_name.empty());
}

// ---------------------------------------------------------------------------
// Test 2: Load pipeline from JSON
// ---------------------------------------------------------------------------
TEST(Pipeline, LoadFromJSON) {
    auto res = vxl::Pipeline::load(project_pipeline_path());
    ASSERT_TRUE(res.ok()) << res.message;

    const auto& steps = res.value.steps();
    ASSERT_EQ(steps.size(), 5u);

    EXPECT_EQ(steps[0].type, vxl::StepType::CAPTURE);
    EXPECT_EQ(steps[0].name, "grab");

    EXPECT_EQ(steps[1].type, vxl::StepType::RECONSTRUCT);
    EXPECT_EQ(steps[1].name, "build_3d");
    EXPECT_EQ(steps[1].params.at("method"), "structured_light");

    EXPECT_EQ(steps[2].type, vxl::StepType::INSPECT_3D);
    EXPECT_EQ(steps[2].name, "check_3d");

    EXPECT_EQ(steps[3].type, vxl::StepType::INSPECT_2D);
    EXPECT_EQ(steps[3].name, "check_2d");

    EXPECT_EQ(steps[4].type, vxl::StepType::OUTPUT);
    EXPECT_EQ(steps[4].name, "result");
}

TEST(Pipeline, LoadNonExistent) {
    auto res = vxl::Pipeline::load("/tmp/no_such_pipeline_vxl.json");
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::FILE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// Test 3: Save / load round-trip
// ---------------------------------------------------------------------------
TEST(Pipeline, SaveAndReload) {
    vxl::Pipeline pipe;
    pipe.set_camera_id("SIM-001");
    pipe.set_recipe("my_recipe.json");

    vxl::PipelineStep s1;
    s1.type = vxl::StepType::CAPTURE;
    s1.name = "grab";
    pipe.add_step(s1);

    vxl::PipelineStep s2;
    s2.type = vxl::StepType::RECONSTRUCT;
    s2.name = "build";
    s2.params["method"] = "structured_light";
    pipe.add_step(s2);

    auto save_res = pipe.save(kTmpPipelinePath);
    ASSERT_TRUE(save_res.ok()) << save_res.message;

    auto load_res = vxl::Pipeline::load(kTmpPipelinePath);
    ASSERT_TRUE(load_res.ok()) << load_res.message;

    const auto& steps = load_res.value.steps();
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].type, vxl::StepType::CAPTURE);
    EXPECT_EQ(steps[0].name, "grab");
    EXPECT_EQ(steps[1].type, vxl::StepType::RECONSTRUCT);
    EXPECT_EQ(steps[1].params.at("method"), "structured_light");

    std::remove(kTmpPipelinePath);
}

// ---------------------------------------------------------------------------
// Test 4: Continuous mode -- start / stop
// ---------------------------------------------------------------------------
TEST(Pipeline, ContinuousMode) {
    // Use a lightweight custom-step-only pipeline so continuous mode
    // completes quickly without the full SimCamera capture/reconstruct
    // cycle (which takes minutes).
    vxl::Pipeline pipe;

    vxl::PipelineStep custom;
    custom.type = vxl::StepType::CUSTOM;
    custom.name = "fast_step";
    pipe.add_step(custom);

    pipe.set_custom_callback("fast_step",
        [](vxl::PipelineContext& ctx) -> vxl::Result<void> {
            // Trivial work -- just mark that we ran.
            ctx.result.ok = true;
            return vxl::Result<void>::success();
        });

    std::atomic<int> cycle_count{0};

    pipe.start([&cycle_count](const vxl::PipelineContext& ctx) {
        (void)ctx;
        cycle_count.fetch_add(1);
    });

    EXPECT_TRUE(pipe.is_running());

    // Wait until at least 2 cycles complete (with a generous timeout)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (cycle_count.load() < 2 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipe.stop();
    EXPECT_FALSE(pipe.is_running());
    EXPECT_GE(cycle_count.load(), 2)
        << "on_complete should have been called at least 2 times";
}

// ---------------------------------------------------------------------------
// Test 5: Empty pipeline returns error
// ---------------------------------------------------------------------------
TEST(Pipeline, EmptyPipelineError) {
    vxl::Pipeline pipe;
    auto res = pipe.run_once();
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Test 6: Invalid JSON step type
// ---------------------------------------------------------------------------
TEST(Pipeline, InvalidStepTypeJSON) {
    const char* bad_json = R"({
        "version": "1.0",
        "camera": "SIM-001",
        "steps": [
            {"type": "nonexistent_type", "name": "bad"}
        ]
    })";

    const char* tmp = "/tmp/vxl_test_bad_pipeline.json";
    {
        std::ofstream ofs(tmp);
        ofs << bad_json;
    }

    auto res = vxl::Pipeline::load(tmp);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::INVALID_PARAMETER);

    std::remove(tmp);
}

// ---------------------------------------------------------------------------
// Test 7: Custom step callback
// ---------------------------------------------------------------------------
TEST(Pipeline, CustomStepCallback) {
    vxl::Pipeline pipe;

    vxl::PipelineStep custom;
    custom.type = vxl::StepType::CUSTOM;
    custom.name = "my_plugin";
    pipe.add_step(custom);

    bool callback_called = false;
    pipe.set_custom_callback("my_plugin",
        [&callback_called](vxl::PipelineContext& ctx) -> vxl::Result<void> {
            callback_called = true;
            ctx.custom_data["plugin_ran"] = true;
            return vxl::Result<void>::success();
        });

    auto res = pipe.run_once();
    ASSERT_TRUE(res.ok()) << res.message;
    EXPECT_TRUE(callback_called);
}

// ---------------------------------------------------------------------------
// Test 8: Custom step without callback returns error
// ---------------------------------------------------------------------------
TEST(Pipeline, CustomStepMissingCallback) {
    vxl::Pipeline pipe;

    vxl::PipelineStep custom;
    custom.type = vxl::StepType::CUSTOM;
    custom.name = "unregistered";
    pipe.add_step(custom);

    auto res = pipe.run_once();
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Test 9: INSPECT_3D without recipe returns error
// ---------------------------------------------------------------------------
TEST(Pipeline, InspectWithoutRecipeError) {
    vxl::Pipeline pipe;
    // No recipe set

    // Provide frames so RECONSTRUCT has input -- but we skip directly
    // to INSPECT_3D which requires a recipe.
    vxl::PipelineStep insp;
    insp.type = vxl::StepType::INSPECT_3D;
    insp.name = "check";
    pipe.add_step(insp);

    auto res = pipe.run_once();
    EXPECT_FALSE(res.ok());
}

} // namespace
