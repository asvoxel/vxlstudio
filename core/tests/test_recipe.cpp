#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

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

// Helper: path to the test recipe shipped with the project.
static std::string project_recipe_path() {
    // Constructed relative to the build dir; tests typically run from build/.
    // Use an environment variable or hard-code for CI.
    return std::string(VXL_TEST_DATA_DIR) + "/pcb_smt/pcb_model_a.json";
}

static const char* kTmpRecipePath = "/tmp/vxl_test_recipe_roundtrip.json";

} // anonymous namespace

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
TEST(Recipe, LoadFromFile) {
    auto res = vxl::Recipe::load(project_recipe_path());
    ASSERT_TRUE(res.ok()) << res.message;

    EXPECT_EQ(res.value.name(), "PCB Model A");
    EXPECT_EQ(res.value.type(), "pcb_smt");
    EXPECT_EQ(res.value.inspector_configs().size(), 3u);
}

TEST(Recipe, LoadNonExistent) {
    auto res = vxl::Recipe::load("/tmp/no_such_file_vxl_test.json");
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::FILE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// Save / round-trip
// ---------------------------------------------------------------------------
TEST(Recipe, SaveAndReload) {
    auto load_res = vxl::Recipe::load(project_recipe_path());
    ASSERT_TRUE(load_res.ok()) << load_res.message;

    auto save_res = load_res.value.save(kTmpRecipePath);
    ASSERT_TRUE(save_res.ok()) << save_res.message;

    auto reload_res = vxl::Recipe::load(kTmpRecipePath);
    ASSERT_TRUE(reload_res.ok()) << reload_res.message;

    EXPECT_EQ(reload_res.value.name(), "PCB Model A");
    EXPECT_EQ(reload_res.value.inspector_configs().size(), 3u);

    std::remove(kTmpRecipePath);
}

// ---------------------------------------------------------------------------
// Validate
// ---------------------------------------------------------------------------
TEST(Recipe, Validate) {
    auto load_res = vxl::Recipe::load(project_recipe_path());
    ASSERT_TRUE(load_res.ok()) << load_res.message;

    auto val_res = load_res.value.validate();
    EXPECT_TRUE(val_res.ok()) << val_res.message;
}

// ---------------------------------------------------------------------------
// Inspect with synthetic data
// ---------------------------------------------------------------------------
TEST(Recipe, InspectSynthetic) {
    auto load_res = vxl::Recipe::load(project_recipe_path());
    ASSERT_TRUE(load_res.ok()) << load_res.message;

    // Create a height map large enough to cover all ROIs in the recipe.
    // The recipe has ROIs up to (900+50, 700+50) = (950, 750).
    auto hmap = make_flat(1000, 800, 0.1f, 0.05f);

    auto insp_res = load_res.value.inspect(hmap);
    ASSERT_TRUE(insp_res.ok()) << insp_res.message;

    // With a flat height map at 0.1, height_measure should find
    // avg_height = 0.1 which is within [0.05, 0.35] -> pass.
    // Flatness of a flat surface -> ~0 -> pass (warning anyway).
    // height_threshold [0, 0.5] -> all pixels in range -> 0 defects -> pass.
    EXPECT_TRUE(insp_res.value.ok);
    EXPECT_EQ(insp_res.value.recipe_name, "PCB Model A");
}

TEST(Recipe, InspectSynthetic_NG) {
    auto load_res = vxl::Recipe::load(project_recipe_path());
    ASSERT_TRUE(load_res.ok()) << load_res.message;

    // Height 0.5: exceeds max_height_mm (0.35) for solder_height -> NG (critical).
    auto hmap = make_flat(1000, 800, 0.5f, 0.05f);

    auto insp_res = load_res.value.inspect(hmap);
    ASSERT_TRUE(insp_res.ok()) << insp_res.message;
    EXPECT_FALSE(insp_res.value.ok);
}
