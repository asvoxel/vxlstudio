// Direct depth map to point cloud conversion
//
// For each pixel (u,v) with depth > 0 and depth < max_depth_mm:
//   Z = depth(v,u) in mm
//   X = (u - cx) * Z / fx
//   Y = (v - cy) * Z / fy
//   Add (X, Y, Z) to point cloud

#include "depth_direct.h"
#include "height_map_gen.h"

#include <cmath>
#include <cstring>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl::detail {

Result<ReconstructOutput> depth_to_cloud(
    const Image& depth_map,
    const CalibrationParams& calib,
    float max_depth_mm,
    float resolution_mm)
{
    // Validate input format
    if (depth_map.format != PixelFormat::GRAY16 &&
        depth_map.format != PixelFormat::FLOAT32) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "depth_map must be GRAY16 (uint16 mm) or FLOAT32 (float mm)");
    }

    if (depth_map.width <= 0 || depth_map.height <= 0) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "depth_map has invalid dimensions");
    }

    // Extract camera intrinsics
    const double fx = calib.camera_matrix[0];
    const double fy = calib.camera_matrix[4];
    const double cx = calib.camera_matrix[2];
    const double cy = calib.camera_matrix[5];

    if (fx == 0.0 || fy == 0.0) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Camera focal lengths (fx, fy) must be non-zero");
    }

    const int W = depth_map.width;
    const int H = depth_map.height;

    // Convert to cv::Mat for easy pixel access
    cv::Mat depth_mat = depth_map.to_cv_mat();

    // Collect 3D points
    std::vector<float> points;
    points.reserve(static_cast<size_t>(W) * H * 3);

    // Also build a validity mask (GRAY8, 255=valid)
    cv::Mat validity(H, W, CV_8U, cv::Scalar(0));

    if (depth_map.format == PixelFormat::GRAY16) {
        for (int v = 0; v < H; ++v) {
            const uint16_t* row = depth_mat.ptr<uint16_t>(v);
            uint8_t* mask_row = validity.ptr<uint8_t>(v);
            for (int u = 0; u < W; ++u) {
                float z_mm = static_cast<float>(row[u]);
                if (z_mm > 0.0f && z_mm < max_depth_mm) {
                    float x = static_cast<float>((u - cx) * z_mm / fx);
                    float y = static_cast<float>((v - cy) * z_mm / fy);
                    points.push_back(x);
                    points.push_back(y);
                    points.push_back(z_mm);
                    mask_row[u] = 255;
                }
            }
        }
    } else { // FLOAT32
        for (int v = 0; v < H; ++v) {
            const float* row = depth_mat.ptr<float>(v);
            uint8_t* mask_row = validity.ptr<uint8_t>(v);
            for (int u = 0; u < W; ++u) {
                float z_mm = row[u];
                if (z_mm > 0.0f && z_mm < max_depth_mm) {
                    float x = static_cast<float>((u - cx) * z_mm / fx);
                    float y = static_cast<float>((v - cy) * z_mm / fy);
                    points.push_back(x);
                    points.push_back(y);
                    points.push_back(z_mm);
                    mask_row[u] = 255;
                }
            }
        }
    }

    size_t point_count = points.size() / 3;

    if (point_count == 0) {
        return Result<ReconstructOutput>::failure(
            ErrorCode::RECONSTRUCT_LOW_MODULATION,
            "No valid depth pixels found (all zero, negative, or beyond max_depth)");
    }

    // Build PointCloud
    PointCloud cloud;
    cloud.format = PointFormat::XYZ_FLOAT;
    cloud.point_count = point_count;
    cloud.buffer = SharedBuffer::allocate(point_count * 3 * sizeof(float));
    std::memcpy(cloud.buffer.data(), points.data(), point_count * 3 * sizeof(float));

    // Convert to height map
    HeightMap hmap = cloud_to_height_map(cloud, resolution_mm);

    // Package output
    ReconstructOutput output;
    output.point_cloud = std::move(cloud);
    output.height_map = std::move(hmap);
    output.modulation_mask = Image::from_cv_mat(validity);

    return Result<ReconstructOutput>::success(std::move(output));
}

} // namespace vxl::detail
