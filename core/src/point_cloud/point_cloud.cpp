#include "vxl/point_cloud.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace vxl {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
struct Vec3f {
    float x, y, z;
};

static size_t point_stride(PointFormat fmt) {
    switch (fmt) {
        case PointFormat::XYZ_FLOAT:    return 12;  // 3 * sizeof(float)
        case PointFormat::XYZRGB_FLOAT: return 16;  // padded
    }
    return 12;
}

static Vec3f read_point(const uint8_t* base, size_t index, PointFormat fmt) {
    size_t stride = point_stride(fmt);
    const float* p = reinterpret_cast<const float*>(base + index * stride);
    return {p[0], p[1], p[2]};
}

static void write_point(uint8_t* base, size_t index, PointFormat fmt,
                         const Vec3f& pt) {
    size_t stride = point_stride(fmt);
    float* p = reinterpret_cast<float*>(base + index * stride);
    p[0] = pt.x;
    p[1] = pt.y;
    p[2] = pt.z;
}

static float dist_sq(const Vec3f& a, const Vec3f& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// ---------------------------------------------------------------------------
// filter_statistical  (brute-force KNN)
// ---------------------------------------------------------------------------
Result<PointCloud> PointCloudOps::filter_statistical(const PointCloud& cloud,
                                                      int k_neighbors,
                                                      double std_ratio) {
    if (!cloud.buffer.data() || cloud.point_count == 0) {
        return Result<PointCloud>::failure(ErrorCode::INVALID_PARAMETER,
                                          "PointCloud is empty");
    }
    if (k_neighbors < 1) {
        return Result<PointCloud>::failure(ErrorCode::INVALID_PARAMETER,
                                          "k_neighbors must be >= 1");
    }

    const size_t N = cloud.point_count;
    const size_t k = static_cast<size_t>(std::min(k_neighbors,
                                                   static_cast<int>(N - 1)));
    const uint8_t* data = cloud.buffer.data();

    // Read all points.
    std::vector<Vec3f> pts(N);
    for (size_t i = 0; i < N; ++i) {
        pts[i] = read_point(data, i, cloud.format);
    }

    // Compute mean distance to K nearest neighbors for each point.
    std::vector<double> mean_dists(N, 0.0);

    for (size_t i = 0; i < N; ++i) {
        // Partial sort to find k smallest distances.
        std::vector<float> dists(N);
        for (size_t j = 0; j < N; ++j) {
            dists[j] = (j == i) ? std::numeric_limits<float>::max()
                                : dist_sq(pts[i], pts[j]);
        }
        std::partial_sort(dists.begin(),
                          dists.begin() + static_cast<long>(k),
                          dists.end());
        double sum = 0.0;
        for (size_t j = 0; j < k; ++j) {
            sum += std::sqrt(static_cast<double>(dists[j]));
        }
        mean_dists[i] = sum / static_cast<double>(k);
    }

    // Global mean and std of mean_dists.
    double global_mean = 0.0;
    for (size_t i = 0; i < N; ++i) global_mean += mean_dists[i];
    global_mean /= static_cast<double>(N);

    double global_var = 0.0;
    for (size_t i = 0; i < N; ++i) {
        double diff = mean_dists[i] - global_mean;
        global_var += diff * diff;
    }
    double global_std = std::sqrt(global_var / static_cast<double>(N));

    double threshold = global_mean + std_ratio * global_std;

    // Collect inlier indices.
    std::vector<size_t> inliers;
    inliers.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        if (mean_dists[i] <= threshold) {
            inliers.push_back(i);
        }
    }

    // Build output cloud.
    size_t stride = point_stride(cloud.format);
    PointCloud out;
    out.format = cloud.format;
    out.point_count = inliers.size();
    out.buffer = SharedBuffer::allocate(inliers.size() * stride);

    for (size_t i = 0; i < inliers.size(); ++i) {
        std::memcpy(out.buffer.data() + i * stride,
                    data + inliers[i] * stride,
                    stride);
    }

    return Result<PointCloud>::success(std::move(out));
}

// ---------------------------------------------------------------------------
// downsample_voxel
// ---------------------------------------------------------------------------
Result<PointCloud> PointCloudOps::downsample_voxel(const PointCloud& cloud,
                                                    double voxel_size) {
    if (!cloud.buffer.data() || cloud.point_count == 0) {
        return Result<PointCloud>::failure(ErrorCode::INVALID_PARAMETER,
                                          "PointCloud is empty");
    }
    if (voxel_size <= 0.0) {
        return Result<PointCloud>::failure(ErrorCode::INVALID_PARAMETER,
                                          "voxel_size must be > 0");
    }

    const size_t N = cloud.point_count;
    const uint8_t* data = cloud.buffer.data();
    double inv_vs = 1.0 / voxel_size;

    // Hash: encode (ix, iy, iz) into a single key.
    struct VoxelKey {
        int64_t ix, iy, iz;
        bool operator==(const VoxelKey& o) const {
            return ix == o.ix && iy == o.iy && iz == o.iz;
        }
    };

    struct VoxelKeyHash {
        size_t operator()(const VoxelKey& k) const {
            // Simple hash combine.
            size_t h = std::hash<int64_t>{}(k.ix);
            h ^= std::hash<int64_t>{}(k.iy) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int64_t>{}(k.iz) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct Accum {
        double sx = 0, sy = 0, sz = 0;
        size_t count = 0;
    };

    std::unordered_map<VoxelKey, Accum, VoxelKeyHash> grid;

    for (size_t i = 0; i < N; ++i) {
        Vec3f p = read_point(data, i, cloud.format);
        VoxelKey key;
        key.ix = static_cast<int64_t>(std::floor(p.x * inv_vs));
        key.iy = static_cast<int64_t>(std::floor(p.y * inv_vs));
        key.iz = static_cast<int64_t>(std::floor(p.z * inv_vs));

        auto& acc = grid[key];
        acc.sx += p.x;
        acc.sy += p.y;
        acc.sz += p.z;
        acc.count++;
    }

    // Build output (XYZ_FLOAT always, dropping color for simplicity).
    size_t out_stride = point_stride(PointFormat::XYZ_FLOAT);
    PointCloud out;
    out.format = PointFormat::XYZ_FLOAT;
    out.point_count = grid.size();
    out.buffer = SharedBuffer::allocate(grid.size() * out_stride);

    size_t idx = 0;
    for (auto& [key, acc] : grid) {
        double inv_n = 1.0 / static_cast<double>(acc.count);
        Vec3f avg;
        avg.x = static_cast<float>(acc.sx * inv_n);
        avg.y = static_cast<float>(acc.sy * inv_n);
        avg.z = static_cast<float>(acc.sz * inv_n);
        write_point(out.buffer.data(), idx, PointFormat::XYZ_FLOAT, avg);
        ++idx;
    }

    return Result<PointCloud>::success(std::move(out));
}

} // namespace vxl
