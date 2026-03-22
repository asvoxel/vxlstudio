#include <cstdio>
#include <fstream>

#include <gtest/gtest.h>

#include "vxl/inference.h"
#include "vxl/types.h"

// ---------------------------------------------------------------------------
// load nonexistent model -> FILE_NOT_FOUND error
// ---------------------------------------------------------------------------
TEST(Inference, LoadNonexistent) {
    auto res = vxl::Inference::load("/tmp/nonexistent_model_12345.onnx");
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::FILE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// load with empty path -> INVALID_PARAMETER error
// ---------------------------------------------------------------------------
TEST(Inference, LoadEmptyPath) {
    auto res = vxl::Inference::load("");
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Default-constructed Inference: accessors should return safe defaults
// ---------------------------------------------------------------------------
TEST(Inference, DefaultConstruction) {
    vxl::Inference inf;
    EXPECT_TRUE(inf.model_path().empty());
    EXPECT_TRUE(inf.input_shape().empty());
    EXPECT_TRUE(inf.output_shape().empty());
}

// ---------------------------------------------------------------------------
// Move semantics compile and don't crash
// ---------------------------------------------------------------------------
TEST(Inference, MoveSemantics) {
    vxl::Inference a;
    vxl::Inference b(std::move(a));
    EXPECT_TRUE(b.model_path().empty());

    vxl::Inference c;
    c = std::move(b);
    EXPECT_TRUE(c.model_path().empty());
}

// ---------------------------------------------------------------------------
// run() on default-constructed Inference should fail gracefully
// ---------------------------------------------------------------------------
TEST(Inference, RunWithoutLoad) {
    vxl::Inference inf;
    vxl::Image img = vxl::Image::create(64, 64, vxl::PixelFormat::RGB8);
    auto res = inf.run(img);
    EXPECT_FALSE(res.ok());
}

#ifdef VXL_NO_ONNXRUNTIME
// ---------------------------------------------------------------------------
// Stub: run always returns MODEL_LOAD_FAILED when ONNX Runtime is absent
// ---------------------------------------------------------------------------
TEST(Inference, StubRunReturnsModelLoadFailed) {
    vxl::Inference inf;
    vxl::Image img = vxl::Image::create(32, 32, vxl::PixelFormat::GRAY8);
    auto res = inf.run(img);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::MODEL_LOAD_FAILED);
}
#endif

#ifndef VXL_NO_ONNXRUNTIME
// ---------------------------------------------------------------------------
// With real ORT: loading a file that exists but is not a valid ONNX model
// should return MODEL_LOAD_FAILED (not crash).
// ---------------------------------------------------------------------------
TEST(Inference, LoadInvalidFile) {
    // Create a tiny temp file that is NOT a valid ONNX model
    const char* path = "/tmp/vxl_test_not_onnx.onnx";
    {
        std::ofstream f(path);
        f << "this is not an onnx model";
    }

    auto res = vxl::Inference::load(path);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.code, vxl::ErrorCode::MODEL_LOAD_FAILED);

    std::remove(path);
}
#endif
