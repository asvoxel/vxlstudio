// VxlStudio -- GuidanceEngine: robot grasp pose computation
// SPDX-License-Identifier: MIT

#include "vxl/guidance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

namespace vxl {

// ---------------------------------------------------------------------------
// Impl (pimpl)
// ---------------------------------------------------------------------------
struct GuidanceEngine::Impl {
    // Reserved for future state (learned models, caches, etc.)
};

GuidanceEngine::GuidanceEngine() : impl_(std::make_unique<Impl>()) {}
GuidanceEngine::~GuidanceEngine() = default;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a top-down grasp pose at (x, y, z).
// Z axis points down (-Z), X axis points along world X.
static GraspPose make_top_down_pose(double x, double y, double z,
                                     float score, float approach_mm) {
    GraspPose gp;
    gp.score = score;

    gp.pose.translation[0] = x;
    gp.pose.translation[1] = y;
    gp.pose.translation[2] = z + static_cast<double>(approach_mm);

    // Rotation: Z axis pointing down  =>  R = diag(1, -1, -1)
    //   X_gripper = +X_world
    //   Y_gripper = -Y_world
    //   Z_gripper = -Z_world  (pointing downward)
    gp.pose.rotation[0] =  1.0;  gp.pose.rotation[1] =  0.0;  gp.pose.rotation[2] = 0.0;
    gp.pose.rotation[3] =  0.0;  gp.pose.rotation[4] = -1.0;  gp.pose.rotation[5] = 0.0;
    gp.pose.rotation[6] =  0.0;  gp.pose.rotation[7] =  0.0;  gp.pose.rotation[8] = -1.0;

    gp.label = "top_down";
    return gp;
}

// Compute local flatness around a point in a height map.
// Returns a value in [0, 1] where 1 = perfectly flat.
static float local_flatness(const float* data, int W, int H,
                             int cx, int cy, int radius = 3) {
    float sum = 0.0f;
    float sum_sq = 0.0f;
    int count = 0;
    float center_z = data[cy * W + cx];

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = cx + dx;
            int ny = cy + dy;
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            float z = data[ny * W + nx];
            if (std::isnan(z)) continue;
            float diff = z - center_z;
            sum += diff;
            sum_sq += diff * diff;
            ++count;
        }
    }

    if (count < 2) return 0.0f;

    float variance = (sum_sq / count) - (sum / count) * (sum / count);
    // Map variance to [0, 1]: small variance = high flatness.
    // Use a Gaussian-like falloff: exp(-variance / sigma^2)
    constexpr float sigma2 = 4.0f;  // mm^2
    return std::exp(-std::max(0.0f, variance) / sigma2);
}

// ---------------------------------------------------------------------------
// compute_grasp (3D point cloud)
// ---------------------------------------------------------------------------
Result<std::vector<GraspPose>> GuidanceEngine::compute_grasp(
    const PointCloud& cloud,
    const GuidanceParams& params)
{
    if (!cloud.buffer.data() || cloud.point_count == 0) {
        return Result<std::vector<GraspPose>>::failure(
            ErrorCode::INVALID_PARAMETER, "PointCloud is empty");
    }

    if (cloud.format != PointFormat::XYZ_FLOAT) {
        return Result<std::vector<GraspPose>>::failure(
            ErrorCode::INVALID_PARAMETER, "Only XYZ_FLOAT point clouds are supported");
    }

    const float* pts = reinterpret_cast<const float*>(cloud.buffer.data());
    const size_t n = cloud.point_count;

    // Strategy: top_down -- find the N highest points
    if (params.strategy == "top_down") {
        // Build index sorted by Z (descending)
        std::vector<size_t> indices(n);
        std::iota(indices.begin(), indices.end(), 0);
        std::partial_sort(
            indices.begin(),
            indices.begin() + std::min(static_cast<size_t>(params.max_results * 5), n),
            indices.end(),
            [pts](size_t a, size_t b) {
                return pts[a * 3 + 2] > pts[b * 3 + 2];
            });

        // Pick top peaks, skipping points too close to already selected ones
        constexpr float min_dist2 = 25.0f;  // 5mm minimum separation
        std::vector<GraspPose> results;
        results.reserve(static_cast<size_t>(params.max_results));

        float z_max = pts[indices[0] * 3 + 2];
        float z_min = pts[indices[std::min(n - 1, static_cast<size_t>(params.max_results * 5 - 1))] * 3 + 2];
        float z_range = std::max(z_max - z_min, 1.0f);

        for (size_t i = 0; i < std::min(n, static_cast<size_t>(params.max_results * 5)); ++i) {
            size_t idx = indices[i];
            float x = pts[idx * 3 + 0];
            float y = pts[idx * 3 + 1];
            float z = pts[idx * 3 + 2];

            // Check minimum separation from already chosen grasps
            bool too_close = false;
            for (const auto& existing : results) {
                float dx = x - static_cast<float>(existing.pose.translation[0]);
                float dy = y - static_cast<float>(existing.pose.translation[1]);
                if (dx * dx + dy * dy < min_dist2) {
                    too_close = true;
                    break;
                }
            }
            if (too_close) continue;

            // Score by height (higher = better)
            float height_score = (z - z_min) / z_range;

            if (height_score < params.min_score) continue;

            auto gp = make_top_down_pose(
                static_cast<double>(x), static_cast<double>(y), static_cast<double>(z),
                height_score, params.approach_distance_mm);
            results.push_back(std::move(gp));

            if (static_cast<int>(results.size()) >= params.max_results) break;
        }

        // Sort by score descending
        std::sort(results.begin(), results.end(),
                  [](const GraspPose& a, const GraspPose& b) {
                      return a.score > b.score;
                  });

        return Result<std::vector<GraspPose>>::success(std::move(results));
    }

    return Result<std::vector<GraspPose>>::failure(
        ErrorCode::INVALID_PARAMETER,
        "Unknown strategy: " + params.strategy);
}

// ---------------------------------------------------------------------------
// compute_grasp_2d (HeightMap)
// ---------------------------------------------------------------------------
Result<std::vector<GraspPose>> GuidanceEngine::compute_grasp_2d(
    const HeightMap& hmap,
    const GuidanceParams& params)
{
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<std::vector<GraspPose>>::failure(
            ErrorCode::INVALID_PARAMETER, "HeightMap is empty");
    }

    const int W = hmap.width;
    const int H = hmap.height;
    const float* data = reinterpret_cast<const float*>(hmap.buffer.data());

    if (params.strategy != "top_down") {
        return Result<std::vector<GraspPose>>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Unknown strategy: " + params.strategy);
    }

    // Find local maxima in height map
    // A pixel is a local maximum if it is >= all 8-connected neighbours.
    constexpr int suppression_radius = 5;  // non-maximum suppression window

    struct Peak {
        int x, y;
        float z;
    };
    std::vector<Peak> peaks;

    for (int py = suppression_radius; py < H - suppression_radius; ++py) {
        for (int px = suppression_radius; px < W - suppression_radius; ++px) {
            float z = data[py * W + px];
            if (std::isnan(z)) continue;

            bool is_max = true;
            for (int dy = -suppression_radius; dy <= suppression_radius && is_max; ++dy) {
                for (int dx = -suppression_radius; dx <= suppression_radius && is_max; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    float nz = data[(py + dy) * W + (px + dx)];
                    if (!std::isnan(nz) && nz > z) {
                        is_max = false;
                    }
                }
            }

            if (is_max) {
                peaks.push_back({px, py, z});
            }
        }
    }

    if (peaks.empty()) {
        return Result<std::vector<GraspPose>>::success({});
    }

    // Sort by height descending
    std::sort(peaks.begin(), peaks.end(),
              [](const Peak& a, const Peak& b) { return a.z > b.z; });

    // Score by peak prominence and flatness
    float z_max = peaks.front().z;
    float z_min = peaks.back().z;
    float z_range = std::max(z_max - z_min, 1.0f);

    std::vector<GraspPose> results;
    results.reserve(static_cast<size_t>(params.max_results));

    for (const auto& peak : peaks) {
        float height_score = (peak.z - z_min) / z_range;
        float flat_score = local_flatness(data, W, H, peak.x, peak.y);
        float combined_score = 0.6f * height_score + 0.4f * flat_score;

        if (combined_score < params.min_score) continue;

        // Convert pixel coords to world coords
        double wx = hmap.origin_x + peak.x * static_cast<double>(hmap.resolution_mm);
        double wy = hmap.origin_y + peak.y * static_cast<double>(hmap.resolution_mm);
        double wz = static_cast<double>(peak.z);

        auto gp = make_top_down_pose(wx, wy, wz,
                                      combined_score, params.approach_distance_mm);
        results.push_back(std::move(gp));

        if (static_cast<int>(results.size()) >= params.max_results) break;
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const GraspPose& a, const GraspPose& b) {
                  return a.score > b.score;
              });

    return Result<std::vector<GraspPose>>::success(std::move(results));
}

// ---------------------------------------------------------------------------
// find_pick_point
// ---------------------------------------------------------------------------
Result<GraspPose> GuidanceEngine::find_pick_point(
    const HeightMap& hmap, const ROI& roi)
{
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<GraspPose>::failure(
            ErrorCode::INVALID_PARAMETER, "HeightMap is empty");
    }

    // Clamp ROI
    int x0 = std::max(0, roi.x);
    int y0 = std::max(0, roi.y);
    int x1 = std::min(roi.x + roi.w, hmap.width);
    int y1 = std::min(roi.y + roi.h, hmap.height);

    if (x1 <= x0 || y1 <= y0) {
        return Result<GraspPose>::failure(
            ErrorCode::INSPECT_ROI_OUT_OF_BOUNDS,
            "ROI does not overlap the height map");
    }

    const float* data = reinterpret_cast<const float*>(hmap.buffer.data());

    float best_z = -std::numeric_limits<float>::infinity();
    int best_x = x0, best_y = y0;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            float z = data[py * hmap.width + px];
            if (std::isnan(z)) continue;
            if (z > best_z) {
                best_z = z;
                best_x = px;
                best_y = py;
            }
        }
    }

    if (std::isinf(best_z)) {
        return Result<GraspPose>::failure(
            ErrorCode::INVALID_PARAMETER,
            "No valid height values in ROI");
    }

    double wx = hmap.origin_x + best_x * static_cast<double>(hmap.resolution_mm);
    double wy = hmap.origin_y + best_y * static_cast<double>(hmap.resolution_mm);
    double wz = static_cast<double>(best_z);

    GraspPose gp;
    gp.score = 1.0f;
    gp.pose.translation[0] = wx;
    gp.pose.translation[1] = wy;
    gp.pose.translation[2] = wz;
    // Identity rotation (top-down)
    gp.pose.rotation[0] =  1.0;  gp.pose.rotation[1] =  0.0;  gp.pose.rotation[2] = 0.0;
    gp.pose.rotation[3] =  0.0;  gp.pose.rotation[4] = -1.0;  gp.pose.rotation[5] = 0.0;
    gp.pose.rotation[6] =  0.0;  gp.pose.rotation[7] =  0.0;  gp.pose.rotation[8] = -1.0;
    gp.label = "pick_point";

    return Result<GraspPose>::success(std::move(gp));
}

// ---------------------------------------------------------------------------
// camera_to_robot: T_robot = T_hand_eye * T_camera
// ---------------------------------------------------------------------------
Result<Pose6D> GuidanceEngine::camera_to_robot(
    const Pose6D& camera_pose,
    const Pose6D& hand_eye_transform)
{
    Pose6D result;

    // 3x3 matrix multiplication: R_result = R_hand_eye * R_camera
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += hand_eye_transform.rotation[i * 3 + k]
                     * camera_pose.rotation[k * 3 + j];
            }
            result.rotation[i * 3 + j] = sum;
        }
    }

    // Translation: t_result = R_hand_eye * t_camera + t_hand_eye
    for (int i = 0; i < 3; ++i) {
        double sum = hand_eye_transform.translation[i];
        for (int k = 0; k < 3; ++k) {
            sum += hand_eye_transform.rotation[i * 3 + k]
                 * camera_pose.translation[k];
        }
        result.translation[i] = sum;
    }

    return Result<Pose6D>::success(result);
}

} // namespace vxl
