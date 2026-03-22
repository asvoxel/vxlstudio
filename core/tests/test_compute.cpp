// Tests for the GPU compute abstraction layer
//
// Covers:
//   1. CPU engine is always available
//   2. set_compute_backend(CPU) succeeds
//   3. set_compute_backend(CUDA) fails gracefully when CUDA is absent
//   4. available_backends() includes CPU
//   5. CPU engine compute_phase produces correct results (regression)
//   6. CPU engine unwrap_phase produces valid results

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include <opencv2/core.hpp>

#include "vxl/compute.h"

namespace {

const double PI     = CV_PI;
const double TWO_PI = 2.0 * CV_PI;

// Helper: generate a sinusoidal fringe image (same as test_reconstruct.cpp)
cv::Mat generate_fringe(int width, int height, int frequency,
                        int step, int total_steps,
                        float amplitude = 100.0f,
                        float background = 128.0f)
{
    cv::Mat img(height, width, CV_32F);
    double delta = TWO_PI * step / total_steps;

    for (int r = 0; r < height; ++r) {
        float* row = img.ptr<float>(r);
        for (int c = 0; c < width; ++c) {
            double phase = TWO_PI * frequency * c / width;
            row[c] = background + amplitude *
                     static_cast<float>(std::cos(phase + delta));
        }
    }
    return img;
}

// ---------------------------------------------------------------------------
// 1. CPU engine is always available
// ---------------------------------------------------------------------------
TEST(ComputeBackend, CpuAlwaysAvailable)
{
    auto backends = vxl::available_backends();
    ASSERT_FALSE(backends.empty());
    EXPECT_EQ(backends.front(), vxl::ComputeBackend::CPU);
}

// ---------------------------------------------------------------------------
// 2. set_compute_backend(CPU) succeeds
// ---------------------------------------------------------------------------
TEST(ComputeBackend, SetCpuSucceeds)
{
    auto result = vxl::set_compute_backend(vxl::ComputeBackend::CPU);
    EXPECT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(vxl::get_compute_backend(), vxl::ComputeBackend::CPU);
}

// ---------------------------------------------------------------------------
// 3. set_compute_backend(CUDA) fails gracefully when CUDA is not compiled in
// ---------------------------------------------------------------------------
TEST(ComputeBackend, SetCudaFailsGracefully)
{
    auto result = vxl::set_compute_backend(vxl::ComputeBackend::CUDA);
    // If CUDA is actually available this will succeed, otherwise it should
    // fail without crashing.
    if (!result.ok()) {
        EXPECT_EQ(result.code, vxl::ErrorCode::INVALID_PARAMETER);
    }
    // Backend should still be CPU (unchanged on failure)
    if (!result.ok()) {
        EXPECT_EQ(vxl::get_compute_backend(), vxl::ComputeBackend::CPU);
    }
}

// ---------------------------------------------------------------------------
// 4. available_backends() includes CPU
// ---------------------------------------------------------------------------
TEST(ComputeBackend, AvailableBackendsIncludesCpu)
{
    auto backends = vxl::available_backends();
    bool found_cpu = false;
    for (auto b : backends) {
        if (b == vxl::ComputeBackend::CPU) { found_cpu = true; break; }
    }
    EXPECT_TRUE(found_cpu);
}

// ---------------------------------------------------------------------------
// 5. CPU engine compute_phase produces correct results (regression)
// ---------------------------------------------------------------------------
TEST(ComputeBackend, CpuComputePhaseRegression)
{
    // Ensure CPU backend is active
    vxl::set_compute_backend(vxl::ComputeBackend::CPU);
    auto& engine = vxl::compute_engine();

    const int W = 256, H = 256, freq = 4, steps = 4;

    std::vector<cv::Mat> frames(steps);
    for (int k = 0; k < steps; ++k) {
        frames[k] = generate_fringe(W, H, freq, k, steps);
    }

    auto result = engine.compute_phase(frames, steps);
    ASSERT_TRUE(result.ok()) << result.message;

    const cv::Mat& phase = result.value.first;
    const cv::Mat& modulation = result.value.second;

    ASSERT_EQ(phase.rows, H);
    ASSERT_EQ(phase.cols, W);

    // Verify phase at sampled columns
    for (int c = 0; c < W; c += 32) {
        double expected = std::fmod(TWO_PI * freq * c / W, TWO_PI);
        if (expected > PI) expected -= TWO_PI;

        float computed = phase.at<float>(H / 2, c);
        double diff = std::abs(computed - expected);
        if (diff > PI) diff = TWO_PI - diff;

        EXPECT_LT(diff, 0.01)
            << "Phase mismatch at col=" << c;
    }

    // Modulation should be ~amplitude (100)
    float mod_center = modulation.at<float>(H / 2, W / 2);
    EXPECT_NEAR(mod_center, 100.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// 6. CPU engine unwrap_phase produces valid results
// ---------------------------------------------------------------------------
TEST(ComputeBackend, CpuUnwrapPhaseValid)
{
    vxl::set_compute_backend(vxl::ComputeBackend::CPU);
    auto& engine = vxl::compute_engine();

    const int W = 256, H = 64, steps = 4;
    const std::vector<int> frequencies = {1, 4, 16};

    // Build wrapped phases and modulations for each frequency
    std::vector<cv::Mat> wrapped_phases(frequencies.size());
    std::vector<cv::Mat> modulations(frequencies.size());

    for (size_t f = 0; f < frequencies.size(); ++f) {
        std::vector<cv::Mat> frames(steps);
        for (int k = 0; k < steps; ++k) {
            frames[k] = generate_fringe(W, H, frequencies[f], k, steps);
        }
        auto pr = engine.compute_phase(frames, steps);
        ASSERT_TRUE(pr.ok()) << pr.message;
        wrapped_phases[f] = pr.value.first;
        modulations[f]    = pr.value.second;
    }

    auto uw_result = engine.unwrap_phase(
        wrapped_phases, frequencies, modulations, 5.0f);
    ASSERT_TRUE(uw_result.ok()) << uw_result.message;

    const cv::Mat& unwrapped = uw_result.value;
    ASSERT_EQ(unwrapped.rows, H);
    ASSERT_EQ(unwrapped.cols, W);

    // Verify monotonicity in the first half of a row
    int row = H / 2;
    float prev = -1e30f;
    int violations = 0;
    for (int c = 1; c < W / 2; ++c) {
        float val = unwrapped.at<float>(row, c);
        if (std::isnan(val)) continue;
        if (val < prev - 0.01f) ++violations;
        prev = val;
    }
    EXPECT_EQ(violations, 0)
        << "Unwrapped phase should be monotonically increasing (first half)";

    // Range check: first half should span ~8 full periods
    float first_val = unwrapped.at<float>(row, 0);
    float mid_val   = unwrapped.at<float>(row, W / 2 - 1);
    if (!std::isnan(first_val) && !std::isnan(mid_val)) {
        double range = mid_val - first_val;
        double expected_range = TWO_PI * 16.0 * (W / 2 - 1.0) / W;
        EXPECT_NEAR(range, expected_range, TWO_PI * 1.0);
    }
}

// ---------------------------------------------------------------------------
// 7. compute_engine() returns the correct backend type
// ---------------------------------------------------------------------------
TEST(ComputeBackend, EngineTypeMatchesBackend)
{
    vxl::set_compute_backend(vxl::ComputeBackend::CPU);
    EXPECT_EQ(vxl::compute_engine().type(), vxl::ComputeBackend::CPU);
    EXPECT_EQ(vxl::compute_engine().name(), "CPU");
}

} // namespace
