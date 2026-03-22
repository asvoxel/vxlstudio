// Orchestrator: Reconstruct methods and reconstruct() free function
//
// Supports multiple 3D acquisition sources:
//   - from_fringe:  Structured light (fringe projection)
//   - from_stereo:  Stereo matching (left + right cameras)
//   - from_depth:   Direct depth map (e.g., ToF, LiDAR)
//   - process:      Generic dispatch by provider type string

#include "vxl/reconstruct.h"
#include "vxl/compute.h"

#include "depth_direct.h"
#include "height_map_gen.h"
#include "phase_shift.h"
#include "phase_unwrap.h"
#include "post_process.h"
#include "stereo_match.h"
#include "triangulation.h"

#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl {

Result<ReconstructOutput> Reconstruct::from_fringe(
    const std::vector<Image>& frames,
    const CalibrationParams& calib,
    const ReconstructParams& params)
{
    // -----------------------------------------------------------------------
    // 1. Validate inputs
    // -----------------------------------------------------------------------
    const int n_freq = static_cast<int>(params.frequencies.size());
    const int n_steps = params.phase_shift_steps;
    const int expected_frames = n_freq * n_steps;

    if (n_freq < 1) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "At least one frequency is required");
    }

    if (n_steps < 3) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Phase shift steps must be >= 3");
    }

    if (static_cast<int>(frames.size()) != expected_frames) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Expected " + std::to_string(expected_frames) + " frames ("
            + std::to_string(n_freq) + " frequencies x "
            + std::to_string(n_steps) + " steps), got "
            + std::to_string(frames.size()));
    }

    if (frames.empty() || frames[0].width <= 0 || frames[0].height <= 0) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Frames have invalid dimensions");
    }

    // -----------------------------------------------------------------------
    // 2. Compute wrapped phase and modulation for each frequency
    //    (dispatched through the active compute backend)
    // -----------------------------------------------------------------------
    auto& engine = compute_engine();

    std::vector<cv::Mat> wrapped_phases(n_freq);
    std::vector<cv::Mat> modulations(n_freq);

    for (int f = 0; f < n_freq; ++f) {
        // Extract frames for this frequency
        std::vector<cv::Mat> freq_frames(n_steps);
        for (int s = 0; s < n_steps; ++s) {
            int idx = f * n_steps + s;
            cv::Mat mat = frames[idx].to_cv_mat();
            if (mat.channels() > 1) {
                cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
            }
            freq_frames[s] = mat;
        }

        auto phase_result = engine.compute_phase(freq_frames, n_steps);
        if (!phase_result.ok()) {
            return Result<ReconstructOutput>::failure(
                phase_result.code, std::move(phase_result.message));
        }
        wrapped_phases[f] = std::move(phase_result.value.first);
        modulations[f]    = std::move(phase_result.value.second);
    }

    // -----------------------------------------------------------------------
    // 3. Multi-frequency temporal phase unwrapping
    //    (dispatched through the active compute backend)
    // -----------------------------------------------------------------------
    auto unwrap_result = engine.unwrap_phase(
        wrapped_phases, params.frequencies, modulations,
        params.min_modulation);
    if (!unwrap_result.ok()) {
        return Result<ReconstructOutput>::failure(
            unwrap_result.code, std::move(unwrap_result.message));
    }
    cv::Mat unwrapped = std::move(unwrap_result.value);

    // Check that we have enough valid pixels
    int valid_count = 0;
    const int total = unwrapped.rows * unwrapped.cols;
    for (int r = 0; r < unwrapped.rows; ++r) {
        const float* row = unwrapped.ptr<float>(r);
        for (int c = 0; c < unwrapped.cols; ++c) {
            if (!std::isnan(row[c])) ++valid_count;
        }
    }

    if (valid_count == 0) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::RECONSTRUCT_LOW_MODULATION,
            "No valid pixels after phase unwrapping (all below min_modulation)");
    }

    // Build modulation mask (validity) from the highest frequency
    cv::Mat mod_mask(unwrapped.rows, unwrapped.cols, CV_8U);
    {
        const cv::Mat& top_mod = modulations.back();
        for (int r = 0; r < unwrapped.rows; ++r) {
            const float* mrow = top_mod.ptr<float>(r);
            const float* prow = unwrapped.ptr<float>(r);
            uint8_t* orow = mod_mask.ptr<uint8_t>(r);
            for (int c = 0; c < unwrapped.cols; ++c) {
                orow[c] = (!std::isnan(prow[c]) &&
                           mrow[c] >= params.min_modulation) ? 255 : 0;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 4. Phase-to-3D triangulation
    // -----------------------------------------------------------------------
    // Normalize the unwrapped phase so that one full 2*pi corresponds to
    // one projector width. The unwrapped phase at frequency F spans
    // approximately F * 2*pi across the image. We need to divide by F
    // to get the "absolute" phase where 2*pi = full projector width.
    int highest_freq = params.frequencies.back();
    cv::Mat normalized_phase = unwrapped / static_cast<float>(highest_freq);

    PointCloud cloud = detail::phase_to_3d(normalized_phase, calib, mod_mask);

    if (cloud.point_count == 0) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::RECONSTRUCT_LOW_MODULATION,
            "No 3D points generated from triangulation");
    }

    // -----------------------------------------------------------------------
    // 5. Statistical outlier removal
    // -----------------------------------------------------------------------
    PointCloud filtered_cloud = detail::filter_outliers(cloud, 8, 2.0f);

    // -----------------------------------------------------------------------
    // 6. Point cloud to height map
    // -----------------------------------------------------------------------
    HeightMap hmap = detail::cloud_to_height_map(
        filtered_cloud, params.height_map_resolution_mm);

    // -----------------------------------------------------------------------
    // 7. Height map filtering
    // -----------------------------------------------------------------------
    if (!params.filter_type.empty() && params.filter_kernel_size >= 3) {
        hmap = detail::filter_height_map(
            hmap, params.filter_type, params.filter_kernel_size);
    }

    // -----------------------------------------------------------------------
    // 8. Package output
    // -----------------------------------------------------------------------
    ReconstructOutput output;
    output.height_map = std::move(hmap);
    output.point_cloud = std::move(filtered_cloud);
    output.modulation_mask = Image::from_cv_mat(mod_mask);

    return Result<ReconstructOutput>::success(std::move(output));
}

// ---------------------------------------------------------------------------
// from_stereo -- Stereo matching reconstruction
// ---------------------------------------------------------------------------
Result<ReconstructOutput> Reconstruct::from_stereo(
    const Image& left, const Image& right,
    const CalibrationParams& calib,
    const ReconstructParams& params)
{
    return detail::stereo_to_cloud(left, right, calib, params);
}

// ---------------------------------------------------------------------------
// from_depth -- Direct depth map to point cloud
// ---------------------------------------------------------------------------
Result<ReconstructOutput> Reconstruct::from_depth(
    const Image& depth_map,
    const CalibrationParams& calib,
    float max_depth_mm)
{
    // Use a default resolution for the height map
    return detail::depth_to_cloud(depth_map, calib, max_depth_mm, 0.05f);
}

// ---------------------------------------------------------------------------
// Provider registry (static, thread-safe)
// ---------------------------------------------------------------------------
namespace {

std::mutex& provider_mutex() {
    static std::mutex mtx;
    return mtx;
}

std::map<std::string, std::unique_ptr<IDepthProvider>>& provider_map() {
    static std::map<std::string, std::unique_ptr<IDepthProvider>> registry;
    return registry;
}

} // anonymous namespace

void Reconstruct::register_provider(
    const std::string& type,
    std::unique_ptr<IDepthProvider> provider)
{
    std::lock_guard<std::mutex> lock(provider_mutex());
    provider_map()[type] = std::move(provider);
}

// ---------------------------------------------------------------------------
// process -- Generic dispatch by provider type string
// ---------------------------------------------------------------------------
Result<ReconstructOutput> Reconstruct::process(
    const std::string& provider_type,
    const std::vector<Image>& images,
    const CalibrationParams& calib,
    const ReconstructParams& params)
{
    // Built-in: structured light
    if (provider_type == "structured_light") {
        return from_fringe(images, calib, params);
    }

    // Built-in: stereo matching (first two images are left/right)
    if (provider_type == "stereo") {
        if (images.size() < 2) {
            return Result<ReconstructOutput>::failure(
                ErrorCode::INVALID_PARAMETER,
                "Stereo reconstruction requires at least 2 images (left, right)");
        }
        return from_stereo(images[0], images[1], calib, params);
    }

    // Built-in: direct depth map (first image is the depth map)
    if (provider_type == "depth_direct") {
        if (images.empty()) {
            return Result<ReconstructOutput>::failure(
                ErrorCode::INVALID_PARAMETER,
                "depth_direct requires at least 1 image (depth map)");
        }
        return from_depth(images[0], calib);
    }

    // Check registered custom providers
    {
        std::lock_guard<std::mutex> lock(provider_mutex());
        auto& registry = provider_map();
        auto it = registry.find(provider_type);
        if (it != registry.end()) {
            return it->second->process(images, calib, params);
        }
    }

    return Result<ReconstructOutput>::failure(
        ErrorCode::INVALID_PARAMETER,
        "Unknown provider type: '" + provider_type +
        "'. Built-in types: structured_light, stereo, depth_direct");
}

// Convenience free function
Result<HeightMap> reconstruct(
    const std::vector<Image>& frames,
    const CalibrationParams& calib,
    const ReconstructParams& params)
{
    auto result = Reconstruct::from_fringe(frames, calib, params);
    if (!result.ok()) {
        return Result<HeightMap>::failure(result.code, std::move(result.message));
    }
    return Result<HeightMap>::success(std::move(result.value.height_map));
}

} // namespace vxl
