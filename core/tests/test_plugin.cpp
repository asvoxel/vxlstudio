#include <cstring>

#include <gtest/gtest.h>

#include "vxl/plugin_api.h"
#include "vxl/reconstruct.h"
#include "vxl/calibration.h"
#include "vxl/types.h"

namespace vxl {
namespace {

// ---------------------------------------------------------------------------
// PluginManager: default construction yields empty state
// ---------------------------------------------------------------------------
TEST(PluginManager, ConstructionIsEmpty) {
    PluginManager pm;
    EXPECT_TRUE(pm.loaded_plugins().empty());
}

// ---------------------------------------------------------------------------
// PluginManager: is_loaded returns false for unknown name
// ---------------------------------------------------------------------------
TEST(PluginManager, IsLoadedReturnsFalseForUnknown) {
    PluginManager pm;
    EXPECT_FALSE(pm.is_loaded("no_such_plugin"));
}

// ---------------------------------------------------------------------------
// PluginManager: get_plugin_object returns nullptr for unknown name
// ---------------------------------------------------------------------------
TEST(PluginManager, GetPluginObjectNullptrForUnknown) {
    PluginManager pm;
    EXPECT_EQ(pm.get_plugin_object("no_such_plugin"), nullptr);
}

// ---------------------------------------------------------------------------
// PluginManager: get_depth_provider returns nullptr for unknown name
// ---------------------------------------------------------------------------
TEST(PluginManager, GetDepthProviderNullptrForUnknown) {
    PluginManager pm;
    EXPECT_EQ(pm.get_depth_provider("no_such_plugin"), nullptr);
}

// ---------------------------------------------------------------------------
// PluginManager: load nonexistent file returns error
// ---------------------------------------------------------------------------
TEST(PluginManager, LoadNonexistentFileReturnsError) {
    PluginManager pm;
    auto result = pm.load("/tmp/does_not_exist_plugin.so");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, ErrorCode::FILE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// PluginManager: load invalid file (not a shared library) returns error
// ---------------------------------------------------------------------------
TEST(PluginManager, LoadInvalidFileReturnsError) {
    // Create a temporary file that is not a shared library
    const std::string path = "/tmp/vxl_test_invalid_plugin.txt";
    {
        FILE* f = fopen(path.c_str(), "w");
        if (f) {
            fputs("not a shared library", f);
            fclose(f);
        }
    }

    PluginManager pm;
    auto result = pm.load(path);
    EXPECT_FALSE(result.ok());
    // Could be FILE_NOT_FOUND (dlopen fails) or INTERNAL_ERROR (missing symbols)

    // Clean up
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// PluginManager: load_directory on nonexistent directory returns error
// ---------------------------------------------------------------------------
TEST(PluginManager, LoadDirectoryNonexistentReturnsError) {
    PluginManager pm;
    auto result = pm.load_directory("/tmp/no_such_plugin_dir_12345");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, ErrorCode::FILE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// PluginManager: unload unknown name returns error
// ---------------------------------------------------------------------------
TEST(PluginManager, UnloadUnknownReturnsError) {
    PluginManager pm;
    auto result = pm.unload("not_loaded");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// PluginManager: global singleton is accessible and consistent
// ---------------------------------------------------------------------------
TEST(PluginManager, GlobalSingletonIsConsistent) {
    auto& a = plugin_manager();
    auto& b = plugin_manager();
    EXPECT_EQ(&a, &b);
}

// ---------------------------------------------------------------------------
// Integration test: load the sample plugin, inspect metadata, call process()
// ---------------------------------------------------------------------------
#ifdef VXL_TEST_PLUGIN_PATH

TEST(PluginManager, IntegrationLoadSamplePlugin) {
    PluginManager pm;

    // 1. Load the built sample plugin
    auto result = pm.load(VXL_TEST_PLUGIN_PATH);
    ASSERT_TRUE(result.ok())
        << "Failed to load sample plugin at " << VXL_TEST_PLUGIN_PATH
        << ": " << result.message;

    // 2. Verify PluginInfo fields
    const auto& info = result.value;
    EXPECT_EQ(info.name,    "sample_flat_depth");
    EXPECT_EQ(info.version, "1.0.0");
    EXPECT_EQ(info.type,    "depth_provider");
    EXPECT_FALSE(info.author.empty());

    // Plugin should now appear in the loaded list
    EXPECT_TRUE(pm.is_loaded("sample_flat_depth"));
    EXPECT_EQ(pm.loaded_plugins().size(), 1u);

    // 3. Get as IDepthProvider
    IDepthProvider* provider = pm.get_depth_provider("sample_flat_depth");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->type(), "flat_depth");

    // 4. Call process() with a small test image and verify point cloud output
    const int test_w = 8;
    const int test_h = 4;
    auto test_img = Image::create(test_w, test_h, PixelFormat::GRAY8);
    std::memset(test_img.buffer.data(), 128,
                static_cast<size_t>(test_img.stride) * test_img.height);

    std::vector<Image> images;
    images.push_back(std::move(test_img));

    CalibrationParams calib = CalibrationParams::default_sim();
    ReconstructParams params;

    auto proc_result = provider->process(images, calib, params);
    ASSERT_TRUE(proc_result.ok()) << proc_result.message;

    const auto& output = proc_result.value;

    // Point cloud should have test_w * test_h points
    EXPECT_EQ(output.point_cloud.point_count,
              static_cast<size_t>(test_w * test_h));
    EXPECT_EQ(output.point_cloud.format, PointFormat::XYZ_FLOAT);
    EXPECT_NE(output.point_cloud.buffer.data(), nullptr);

    // Verify point data: all z values should be 100.0 (flat depth)
    const float* pts = reinterpret_cast<const float*>(
        output.point_cloud.buffer.data());
    for (size_t i = 0; i < output.point_cloud.point_count; ++i) {
        EXPECT_FLOAT_EQ(pts[i * 3 + 2], 100.0f)
            << "Point " << i << " z should be 100.0";
    }

    // Height map should match dimensions
    EXPECT_EQ(output.height_map.width, test_w);
    EXPECT_EQ(output.height_map.height, test_h);

    // 5. Unload
    auto unload_result = pm.unload("sample_flat_depth");
    EXPECT_TRUE(unload_result.ok());
    EXPECT_FALSE(pm.is_loaded("sample_flat_depth"));
}

#endif // VXL_TEST_PLUGIN_PATH

} // anonymous namespace
} // namespace vxl
