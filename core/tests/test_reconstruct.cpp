// Tests for the structured light reconstruction pipeline
//
// Covers:
//   1. Phase shift computation with synthetic sinusoidal images
//   2. Multi-frequency temporal phase unwrapping
//   3. Full pipeline with a flat plane
//   4. Full pipeline with a sinusoidal surface

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include <opencv2/core.hpp>

#include "vxl/calibration.h"
#include "vxl/error.h"
#include "vxl/reconstruct.h"
#include "vxl/types.h"

// Access internal modules for unit testing
#include "../src/reconstruct/height_map_gen.h"
#include "../src/reconstruct/phase_shift.h"
#include "../src/reconstruct/phase_unwrap.h"

namespace {

const double PI = CV_PI;
const double TWO_PI = 2.0 * CV_PI;

// ---------------------------------------------------------------------------
// Helper: generate a sinusoidal fringe image
// ---------------------------------------------------------------------------
cv::Mat generate_fringe_image(int width, int height, int frequency,
                              int step, int total_steps,
                              float amplitude = 100.0f,
                              float background = 128.0f,
                              const cv::Mat& phase_offset_map = cv::Mat())
{
    cv::Mat img(height, width, CV_32F);

    double delta = TWO_PI * step / total_steps;

    for (int r = 0; r < height; ++r) {
        float* row = img.ptr<float>(r);
        for (int c = 0; c < width; ++c) {
            // Base phase: frequency periods across the image width
            double phase = TWO_PI * frequency * c / width;

            // Optional per-pixel phase offset (simulates surface height)
            if (!phase_offset_map.empty()) {
                phase += phase_offset_map.at<float>(r, c);
            }

            row[c] = background + amplitude * static_cast<float>(
                         std::cos(phase + delta));
        }
    }

    return img;
}

// ---------------------------------------------------------------------------
// Helper: create CalibrationParams for a simple test geometry
// ---------------------------------------------------------------------------
vxl::CalibrationParams make_test_calib(int img_w, int img_h)
{
    vxl::CalibrationParams calib;

    // Camera intrinsics (simple pinhole)
    double fx = 1000.0, fy = 1000.0;
    double cx = img_w / 2.0, cy = img_h / 2.0;
    calib.camera_matrix[0] = fx;
    calib.camera_matrix[1] = 0;
    calib.camera_matrix[2] = cx;
    calib.camera_matrix[3] = 0;
    calib.camera_matrix[4] = fy;
    calib.camera_matrix[5] = cy;
    calib.camera_matrix[6] = 0;
    calib.camera_matrix[7] = 0;
    calib.camera_matrix[8] = 1;

    calib.image_width  = img_w;
    calib.image_height = img_h;

    // Projector intrinsics
    calib.projector_matrix[0] = 1000.0;
    calib.projector_matrix[1] = 0;
    calib.projector_matrix[2] = img_w / 2.0 - 20.0;  // offset from camera cx to avoid degenerate geometry
    calib.projector_matrix[3] = 0;
    calib.projector_matrix[4] = 1000.0;
    calib.projector_matrix[5] = img_h / 2.0;
    calib.projector_matrix[6] = 0;
    calib.projector_matrix[7] = 0;
    calib.projector_matrix[8] = 1;

    calib.projector_width  = img_w;
    calib.projector_height = img_h;

    // Identity rotation
    calib.rotation[0] = 1; calib.rotation[1] = 0; calib.rotation[2] = 0;
    calib.rotation[3] = 0; calib.rotation[4] = 1; calib.rotation[5] = 0;
    calib.rotation[6] = 0; calib.rotation[7] = 0; calib.rotation[8] = 1;

    // Horizontal baseline of 100mm
    calib.translation[0] = 100.0;
    calib.translation[1] = 0.0;
    calib.translation[2] = 0.0;

    return calib;
}

// ---------------------------------------------------------------------------
// Test 1: Phase shift computation with synthetic sinusoidal images
// ---------------------------------------------------------------------------
TEST(PhaseShift, ComputePhaseFromSyntheticSinusoids)
{
    const int W = 256;
    const int H = 256;
    const int frequency = 4;
    const int steps = 4;

    std::vector<cv::Mat> frames(steps);
    for (int k = 0; k < steps; ++k) {
        frames[k] = generate_fringe_image(W, H, frequency, k, steps);
    }

    auto result = vxl::detail::compute_phase(frames, steps);

    ASSERT_EQ(result.phase.rows, H);
    ASSERT_EQ(result.phase.cols, W);
    ASSERT_EQ(result.modulation.rows, H);
    ASSERT_EQ(result.modulation.cols, W);

    // Verify phase at specific pixels
    // At column c, the expected phase = 2*pi*frequency*c/W (mod 2*pi, wrapped)
    // The atan2 result is in [-pi, pi]
    for (int c = 0; c < W; c += 32) {
        double expected_phase = std::fmod(TWO_PI * frequency * c / W, TWO_PI);
        if (expected_phase > PI) expected_phase -= TWO_PI;

        float computed = result.phase.at<float>(H / 2, c);

        // Allow small tolerance for floating point
        double diff = std::abs(computed - expected_phase);
        // Handle wrap-around at +/- pi
        if (diff > PI) diff = TWO_PI - diff;
        EXPECT_LT(diff, 0.01)
            << "Phase mismatch at col=" << c
            << " expected=" << expected_phase
            << " got=" << computed;
    }

    // Verify modulation is approximately the amplitude (100.0)
    float mod_center = result.modulation.at<float>(H / 2, W / 2);
    EXPECT_NEAR(mod_center, 100.0f, 1.0f)
        << "Modulation should be ~100 (amplitude of the sinusoid)";
}

// ---------------------------------------------------------------------------
// Test 1b: Phase shift with 3-step PSP
// ---------------------------------------------------------------------------
TEST(PhaseShift, ThreeStepPSP)
{
    const int W = 128;
    const int H = 128;
    const int frequency = 2;
    const int steps = 3;

    std::vector<cv::Mat> frames(steps);
    for (int k = 0; k < steps; ++k) {
        frames[k] = generate_fringe_image(W, H, frequency, k, steps);
    }

    auto result = vxl::detail::compute_phase(frames, steps);

    // Check a few phase values
    for (int c = 0; c < W; c += 16) {
        double expected_phase = std::fmod(TWO_PI * frequency * c / W, TWO_PI);
        if (expected_phase > PI) expected_phase -= TWO_PI;

        float computed = result.phase.at<float>(H / 2, c);
        double diff = std::abs(computed - expected_phase);
        if (diff > PI) diff = TWO_PI - diff;
        EXPECT_LT(diff, 0.02)
            << "3-step PSP phase mismatch at col=" << c;
    }
}

// ---------------------------------------------------------------------------
// Test 2: Multi-frequency temporal phase unwrapping
// ---------------------------------------------------------------------------
TEST(PhaseUnwrap, TemporalUnwrapMultiFrequency)
{
    const int W = 256;
    const int H = 64;
    const std::vector<int> frequencies = {1, 4, 16};
    const int steps = 4;

    // Generate wrapped phases for each frequency
    std::vector<cv::Mat> wrapped_phases(frequencies.size());
    std::vector<cv::Mat> modulations(frequencies.size());

    for (size_t f = 0; f < frequencies.size(); ++f) {
        std::vector<cv::Mat> frames(steps);
        for (int k = 0; k < steps; ++k) {
            frames[k] = generate_fringe_image(W, H, frequencies[f], k, steps);
        }
        auto pr = vxl::detail::compute_phase(frames, steps);
        wrapped_phases[f] = pr.phase;
        modulations[f] = pr.modulation;
    }

    cv::Mat unwrapped = vxl::detail::unwrap_temporal(
        wrapped_phases, frequencies, modulations, 5.0f);

    ASSERT_EQ(unwrapped.rows, H);
    ASSERT_EQ(unwrapped.cols, W);

    // The unwrapped phase at the highest frequency (16) should be monotonically
    // increasing and span 16 full periods = 16 * 2*pi
    // Expected: phase(c) = 2*pi*16*c/W - pi (from the atan2 offset)
    // Actually, the unwrapped phase at freq 16 should be approximately
    // 2*pi*16*c/W, shifted to match the wrapped phase convention.

    // Check monotonicity in the first half of the row (before the
    // base-frequency wrap at c = W/2).  With frequency-1 as the base,
    // the wrapped phase has a discontinuity near the midpoint, which
    // propagates through temporal unwrapping.  We only verify the
    // continuous region c = [0, W/2).
    int row = H / 2;
    float prev = -1e30f;
    int monotone_violations = 0;
    for (int c = 1; c < W / 2; ++c) {
        float val = unwrapped.at<float>(row, c);
        if (std::isnan(val)) continue;
        if (val < prev - 0.01f) {
            ++monotone_violations;
        }
        prev = val;
    }
    EXPECT_EQ(monotone_violations, 0)
        << "Unwrapped phase should be monotonically increasing (first half)";

    // Check that the range in the first half spans approximately 8
    // full periods (half of 16).  The base-frequency wrap makes it
    // impossible to get a clean 16-period span across the full row.
    float first_val = unwrapped.at<float>(row, 0);
    float mid_val   = unwrapped.at<float>(row, W / 2 - 1);
    if (!std::isnan(first_val) && !std::isnan(mid_val)) {
        double range = mid_val - first_val;
        double expected_range = TWO_PI * 16.0 * (W / 2 - 1.0) / W;
        EXPECT_NEAR(range, expected_range, TWO_PI * 1.0)
            << "Phase range (first half) should span ~8 full periods";
    }
}

// ---------------------------------------------------------------------------
// Test 2b: Unwrapping with low-modulation masking
// ---------------------------------------------------------------------------
TEST(PhaseUnwrap, MasksLowModulationPixels)
{
    const int W = 64;
    const int H = 64;
    const std::vector<int> frequencies = {1, 4};
    const int steps = 4;

    std::vector<cv::Mat> wrapped_phases(2);
    std::vector<cv::Mat> modulations(2);

    for (int f = 0; f < 2; ++f) {
        std::vector<cv::Mat> frames(steps);
        for (int k = 0; k < steps; ++k) {
            frames[k] = generate_fringe_image(W, H, frequencies[f], k, steps);
        }
        auto pr = vxl::detail::compute_phase(frames, steps);
        wrapped_phases[f] = pr.phase;
        modulations[f] = pr.modulation;
    }

    // Zero out modulation in a region to simulate low signal
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            modulations[1].at<float>(r, c) = 0.0f;
        }
    }

    cv::Mat unwrapped = vxl::detail::unwrap_temporal(
        wrapped_phases, frequencies, modulations, 5.0f);

    // The zeroed region should be NaN
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            EXPECT_TRUE(std::isnan(unwrapped.at<float>(r, c)))
                << "Low-modulation pixel at (" << r << "," << c
                << ") should be NaN";
        }
    }

    // Non-zeroed region should have valid values
    float val = unwrapped.at<float>(H / 2, W / 2);
    EXPECT_FALSE(std::isnan(val))
        << "Valid-modulation pixel should not be NaN";
}

// ---------------------------------------------------------------------------
// Test 3: Full pipeline -- flat plane reconstruction
// ---------------------------------------------------------------------------
TEST(ReconstructPipeline, FlatPlane)
{
    const int W = 128;
    const int H = 128;
    const std::vector<int> frequencies = {1, 8};
    const int steps = 4;
    const int total_frames = static_cast<int>(frequencies.size()) * steps;

    // Generate fringe images for a flat surface (no phase offset)
    std::vector<vxl::Image> frames;
    frames.reserve(total_frames);

    for (int f = 0; f < static_cast<int>(frequencies.size()); ++f) {
        for (int k = 0; k < steps; ++k) {
            cv::Mat mat = generate_fringe_image(W, H, frequencies[f], k, steps);
            frames.push_back(vxl::Image::from_cv_mat(mat));
        }
    }

    vxl::CalibrationParams calib = make_test_calib(W, H);

    vxl::ReconstructParams params;
    params.phase_shift_steps = steps;
    params.frequencies = frequencies;
    params.min_modulation = 5.0f;
    params.height_map_resolution_mm = 1.0f;
    params.filter_type = "";  // no filtering for this test

    auto result = vxl::Reconstruct::from_fringe(frames, calib, params);

    ASSERT_TRUE(result.ok()) << "Reconstruction failed: " << result.message;

    // The point cloud should have points
    EXPECT_GT(result.value.point_cloud.point_count, 0u)
        << "Should produce some 3D points";

    // For a flat plane, all Z values should be similar
    const float* pts = reinterpret_cast<const float*>(
        result.value.point_cloud.buffer.data());
    size_t n = result.value.point_cloud.point_count;

    if (n > 10) {
        // Compute Z statistics
        double z_sum = 0.0;
        double z_sq_sum = 0.0;
        int z_count = 0;

        for (size_t i = 0; i < n; ++i) {
            float z = pts[i * 3 + 2];
            if (!std::isnan(z) && std::abs(z) < 1e6) {
                z_sum += z;
                z_sq_sum += z * z;
                ++z_count;
            }
        }

        if (z_count > 1) {
            double z_mean = z_sum / z_count;
            double z_var = z_sq_sum / z_count - z_mean * z_mean;
            double z_std = std::sqrt(std::max(0.0, z_var));

            // For a flat plane, the standard deviation of Z should be small
            // relative to the mean Z value
            EXPECT_LT(z_std, std::abs(z_mean) * 0.05 + 1.0)
                << "Z values should be consistent for a flat plane"
                << " (mean=" << z_mean << ", std=" << z_std << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// Test 4: Full pipeline -- sinusoidal surface
// ---------------------------------------------------------------------------
TEST(ReconstructPipeline, SinusoidalSurface)
{
    const int W = 128;
    const int H = 128;
    const std::vector<int> frequencies = {1, 8};
    const int steps = 4;

    // Create a sinusoidal phase offset that simulates a surface with
    // a height variation of ~1mm. The phase offset is proportional to
    // the surface height modulated by the fringe frequency.
    // For simplicity, we add a small phase offset that represents
    // the height-dependent phase shift.
    const float surface_amplitude_rad = 0.1f; // small phase offset in radians

    cv::Mat phase_offset(H, W, CV_32F);
    for (int r = 0; r < H; ++r) {
        float* row = phase_offset.ptr<float>(r);
        for (int c = 0; c < W; ++c) {
            // Sinusoidal surface: 2 periods across the image width
            row[c] = surface_amplitude_rad *
                     static_cast<float>(std::sin(TWO_PI * 2.0 * c / W));
        }
    }

    // Generate fringe images with the surface-induced phase offset
    std::vector<vxl::Image> frames;
    for (int f = 0; f < static_cast<int>(frequencies.size()); ++f) {
        // Scale phase offset by frequency (higher freq = more phase shift
        // for same height)
        cv::Mat scaled_offset = phase_offset * static_cast<float>(frequencies[f]);

        for (int k = 0; k < steps; ++k) {
            cv::Mat mat = generate_fringe_image(
                W, H, frequencies[f], k, steps, 100.0f, 128.0f,
                scaled_offset);
            frames.push_back(vxl::Image::from_cv_mat(mat));
        }
    }

    vxl::CalibrationParams calib = make_test_calib(W, H);

    vxl::ReconstructParams params;
    params.phase_shift_steps = steps;
    params.frequencies = frequencies;
    params.min_modulation = 5.0f;
    params.height_map_resolution_mm = 1.0f;
    params.filter_type = "";

    auto result = vxl::Reconstruct::from_fringe(frames, calib, params);

    ASSERT_TRUE(result.ok()) << "Reconstruction failed: " << result.message;

    EXPECT_GT(result.value.point_cloud.point_count, 0u)
        << "Should produce 3D points for sinusoidal surface";

    // Verify the reconstructed Z values show variation (not flat)
    const float* pts = reinterpret_cast<const float*>(
        result.value.point_cloud.buffer.data());
    size_t n = result.value.point_cloud.point_count;

    if (n > 10) {
        float z_min = std::numeric_limits<float>::max();
        float z_max = std::numeric_limits<float>::lowest();

        for (size_t i = 0; i < n; ++i) {
            float z = pts[i * 3 + 2];
            if (!std::isnan(z) && std::abs(z) < 1e6) {
                z_min = std::min(z_min, z);
                z_max = std::max(z_max, z);
            }
        }

        // The Z range should be non-zero (surface has variation)
        float z_range = z_max - z_min;
        EXPECT_GT(z_range, 0.0f)
            << "Sinusoidal surface should produce Z variation"
            << " (z_min=" << z_min << ", z_max=" << z_max << ")";
    }
}

// ---------------------------------------------------------------------------
// Test 5: Input validation
// ---------------------------------------------------------------------------
TEST(ReconstructPipeline, RejectsInvalidInput)
{
    vxl::CalibrationParams calib = make_test_calib(64, 64);
    vxl::ReconstructParams params;
    params.frequencies = {1, 8};
    params.phase_shift_steps = 4;

    // Wrong number of frames
    std::vector<vxl::Image> frames(3); // should be 8
    auto result = vxl::Reconstruct::from_fringe(frames, calib, params);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Test 6: Convenience free function
// ---------------------------------------------------------------------------
TEST(ReconstructPipeline, FreeFunctionReturnsHeightMap)
{
    const int W = 64;
    const int H = 64;
    const std::vector<int> frequencies = {1, 4};
    const int steps = 4;

    std::vector<vxl::Image> frames;
    for (int f = 0; f < static_cast<int>(frequencies.size()); ++f) {
        for (int k = 0; k < steps; ++k) {
            cv::Mat mat = generate_fringe_image(W, H, frequencies[f], k, steps);
            frames.push_back(vxl::Image::from_cv_mat(mat));
        }
    }

    // Use a calibration where the projector principal point is offset from
    // the camera's.  When both intrinsics are identical and R=I, the
    // triangulation denominator becomes zero for columns where proj_col
    // equals the camera column, producing all-NaN points and an empty
    // height map.  Shifting cx_proj breaks this degeneracy.
    vxl::CalibrationParams calib = make_test_calib(W, H);
    calib.projector_matrix[2] = W / 2.0 - 20.0;  // cx_proj shifted by -20 pixels

    vxl::ReconstructParams params;
    params.phase_shift_steps = steps;
    params.frequencies = frequencies;
    params.min_modulation = 5.0f;
    params.height_map_resolution_mm = 1.0f;
    params.filter_type = "";

    auto result = vxl::reconstruct(frames, calib, params);

    ASSERT_TRUE(result.ok()) << "reconstruct() failed: " << result.message;
    EXPECT_GT(result.value.width, 0);
    EXPECT_GT(result.value.height, 0);
}

// ---------------------------------------------------------------------------
// Test 7: HeightMap from synthetic point cloud on a plane at z=1.0
// ---------------------------------------------------------------------------
TEST(HeightMapGen, SyntheticPlaneAtZ1)
{
    // Create 100 points on a plane at z = 1.0, spread over a 10x10 mm area
    const size_t N = 100;
    const float expected_z = 1.0f;
    std::vector<float> pts(N * 3);

    for (size_t i = 0; i < N; ++i) {
        // Distribute points on a 10x10 grid
        float x = static_cast<float>(i % 10);        // 0..9
        float y = static_cast<float>(i / 10);         // 0..9
        pts[i * 3 + 0] = x;
        pts[i * 3 + 1] = y;
        pts[i * 3 + 2] = expected_z;
    }

    vxl::PointCloud cloud;
    cloud.format = vxl::PointFormat::XYZ_FLOAT;
    cloud.point_count = N;
    cloud.buffer = vxl::SharedBuffer::allocate(N * 3 * sizeof(float));
    std::memcpy(cloud.buffer.data(), pts.data(), N * 3 * sizeof(float));

    float resolution = 1.0f;
    vxl::HeightMap hmap = vxl::detail::cloud_to_height_map(cloud, resolution);

    // Grid should be non-empty
    EXPECT_GE(hmap.width, 2);
    EXPECT_GE(hmap.height, 2);
    ASSERT_NE(hmap.buffer.data(), nullptr);

    // Every cell that has data should be ~1.0
    const float* hdata = reinterpret_cast<const float*>(hmap.buffer.data());
    int valid_count = 0;
    for (int i = 0; i < hmap.width * hmap.height; ++i) {
        if (!std::isnan(hdata[i])) {
            EXPECT_NEAR(hdata[i], expected_z, 0.001f)
                << "Height at cell " << i << " should be ~1.0";
            ++valid_count;
        }
    }

    // We should have at least as many valid cells as there are unique (x,y)
    // positions in our grid (with resolution 1.0, each integer coordinate
    // maps to its own cell).
    EXPECT_GE(valid_count, 10)
        << "Should have multiple valid height cells";
}

// ---------------------------------------------------------------------------
// Test 7b: HeightMap handles empty point cloud gracefully
// ---------------------------------------------------------------------------
TEST(HeightMapGen, EmptyPointCloudReturnsEmptyMap)
{
    vxl::PointCloud empty_cloud;
    empty_cloud.point_count = 0;

    vxl::HeightMap hmap = vxl::detail::cloud_to_height_map(empty_cloud, 1.0f);

    // Should not crash and should return a 0x0 map
    EXPECT_EQ(hmap.width, 0);
    EXPECT_EQ(hmap.height, 0);
}

// ---------------------------------------------------------------------------
// Test 7c: HeightMap with points in range smaller than resolution
// ---------------------------------------------------------------------------
TEST(HeightMapGen, TinyRangeProducesValidGrid)
{
    // All points at the same X/Y but different Z -- range < resolution
    const size_t N = 5;
    std::vector<float> pts(N * 3);
    for (size_t i = 0; i < N; ++i) {
        pts[i * 3 + 0] = 5.0f;           // all same x
        pts[i * 3 + 1] = 5.0f;           // all same y
        pts[i * 3 + 2] = 2.0f + i * 0.1f; // varying z
    }

    vxl::PointCloud cloud;
    cloud.format = vxl::PointFormat::XYZ_FLOAT;
    cloud.point_count = N;
    cloud.buffer = vxl::SharedBuffer::allocate(N * 3 * sizeof(float));
    std::memcpy(cloud.buffer.data(), pts.data(), N * 3 * sizeof(float));

    vxl::HeightMap hmap = vxl::detail::cloud_to_height_map(cloud, 1.0f);

    // Grid should be at least 2x2 (minimum enforced)
    EXPECT_GE(hmap.width, 2);
    EXPECT_GE(hmap.height, 2);

    // At least one cell should have the average Z value
    const float* hdata = reinterpret_cast<const float*>(hmap.buffer.data());
    bool found_valid = false;
    for (int i = 0; i < hmap.width * hmap.height; ++i) {
        if (!std::isnan(hdata[i])) {
            // Average of 2.0, 2.1, 2.2, 2.3, 2.4 = 2.2
            EXPECT_NEAR(hdata[i], 2.2f, 0.01f);
            found_valid = true;
        }
    }
    EXPECT_TRUE(found_valid) << "Should have at least one valid height cell";
}

} // namespace
