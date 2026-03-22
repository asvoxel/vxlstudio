// Internal module: Post-processing filters
//
// Statistical outlier removal uses a brute-force k-NN approach. For large
// point clouds, consider replacing with a KD-tree (e.g., nanoflann).
//
// Height map filtering uses OpenCV's medianBlur and GaussianBlur, with special
// handling of NaN values (replaced with 0 during filtering, then restored).

#include "post_process.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl::detail {

// Grid-based spatial lookup for efficient neighbor search on large clouds.
namespace {

struct SpatialGrid {
    float cell_size;
    float x_min, y_min, z_min;
    int nx, ny, nz;
    // Each cell stores indices of the points it contains.
    std::vector<std::vector<size_t>> cells;

    static SpatialGrid build(const float* pts, size_t n, float cell_size) {
        SpatialGrid g;
        g.cell_size = cell_size;

        g.x_min = std::numeric_limits<float>::max();
        g.y_min = std::numeric_limits<float>::max();
        g.z_min = std::numeric_limits<float>::max();
        float x_max = std::numeric_limits<float>::lowest();
        float y_max = std::numeric_limits<float>::lowest();
        float z_max = std::numeric_limits<float>::lowest();

        for (size_t i = 0; i < n; ++i) {
            float x = pts[i * 3 + 0], y = pts[i * 3 + 1], z = pts[i * 3 + 2];
            if (std::isnan(x)) continue;
            g.x_min = std::min(g.x_min, x); x_max = std::max(x_max, x);
            g.y_min = std::min(g.y_min, y); y_max = std::max(y_max, y);
            g.z_min = std::min(g.z_min, z); z_max = std::max(z_max, z);
        }

        g.nx = std::max(1, static_cast<int>(std::ceil((x_max - g.x_min) / cell_size)) + 1);
        g.ny = std::max(1, static_cast<int>(std::ceil((y_max - g.y_min) / cell_size)) + 1);
        g.nz = std::max(1, static_cast<int>(std::ceil((z_max - g.z_min) / cell_size)) + 1);

        // Cap grid dimensions to avoid excessive memory use
        const int max_cells = 512;
        g.nx = std::min(g.nx, max_cells);
        g.ny = std::min(g.ny, max_cells);
        g.nz = std::min(g.nz, max_cells);

        g.cells.resize(static_cast<size_t>(g.nx) * g.ny * g.nz);

        for (size_t i = 0; i < n; ++i) {
            int ci = g.cell_index(pts[i * 3 + 0], pts[i * 3 + 1], pts[i * 3 + 2]);
            if (ci >= 0) g.cells[ci].push_back(i);
        }

        return g;
    }

    int cell_index(float x, float y, float z) const {
        if (std::isnan(x)) return -1;
        int ix = std::min(static_cast<int>((x - x_min) / cell_size), nx - 1);
        int iy = std::min(static_cast<int>((y - y_min) / cell_size), ny - 1);
        int iz = std::min(static_cast<int>((z - z_min) / cell_size), nz - 1);
        ix = std::max(0, ix); iy = std::max(0, iy); iz = std::max(0, iz);
        return (iz * ny + iy) * nx + ix;
    }

    // Find k nearest neighbors of point i using grid locality.
    float mean_k_distance(const float* pts, size_t i, int k) const {
        float xi = pts[i * 3 + 0], yi = pts[i * 3 + 1], zi = pts[i * 3 + 2];
        int cx = std::min(static_cast<int>((xi - x_min) / cell_size), nx - 1);
        int cy = std::min(static_cast<int>((yi - y_min) / cell_size), ny - 1);
        int cz = std::min(static_cast<int>((zi - z_min) / cell_size), nz - 1);
        cx = std::max(0, cx); cy = std::max(0, cy); cz = std::max(0, cz);

        std::vector<float> dists;
        dists.reserve(k * 27);

        // Expand search radius until we have enough neighbors
        for (int radius = 1; radius <= std::max({nx, ny, nz}); ++radius) {
            dists.clear();
            int r = radius;
            for (int dz = -r; dz <= r; ++dz) {
                int iz = cz + dz;
                if (iz < 0 || iz >= nz) continue;
                for (int dy = -r; dy <= r; ++dy) {
                    int iy = cy + dy;
                    if (iy < 0 || iy >= ny) continue;
                    for (int dx = -r; dx <= r; ++dx) {
                        int ix = cx + dx;
                        if (ix < 0 || ix >= nx) continue;
                        int ci = (iz * ny + iy) * nx + ix;
                        for (size_t j : cells[ci]) {
                            if (j == i) continue;
                            float ddx = pts[j * 3 + 0] - xi;
                            float ddy = pts[j * 3 + 1] - yi;
                            float ddz = pts[j * 3 + 2] - zi;
                            dists.push_back(std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz));
                        }
                    }
                }
            }
            if (static_cast<int>(dists.size()) >= k) break;
        }

        if (dists.empty()) return 0.0f;

        size_t kk = std::min(static_cast<size_t>(k), dists.size());
        std::partial_sort(dists.begin(), dists.begin() + kk, dists.end());
        float sum = 0.0f;
        for (size_t d = 0; d < kk; ++d) sum += dists[d];
        return sum / static_cast<float>(kk);
    }
};

} // anonymous namespace

PointCloud filter_outliers(
    const PointCloud& cloud,
    int k_neighbors,
    float std_ratio)
{
    if (cloud.point_count <= static_cast<size_t>(k_neighbors)) {
        return cloud; // not enough points to filter
    }

    const size_t n = cloud.point_count;
    const float* pts = reinterpret_cast<const float*>(cloud.buffer.data());

    // Compute mean distance to k nearest neighbors for each point.
    // Use a spatial grid to accelerate neighbor lookup for large clouds.
    std::vector<float> mean_distances(n);

    // Estimate a reasonable cell size: use the median inter-point spacing.
    // Approximation: assume points are roughly uniformly distributed and
    // compute the average spacing from the bounding box volume / n.
    float x_min = std::numeric_limits<float>::max(), x_max = std::numeric_limits<float>::lowest();
    float y_min = std::numeric_limits<float>::max(), y_max = std::numeric_limits<float>::lowest();
    float z_min = std::numeric_limits<float>::max(), z_max = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i < n; ++i) {
        float x = pts[i*3], y = pts[i*3+1], z = pts[i*3+2];
        if (std::isnan(x)) continue;
        x_min = std::min(x_min, x); x_max = std::max(x_max, x);
        y_min = std::min(y_min, y); y_max = std::max(y_max, y);
        z_min = std::min(z_min, z); z_max = std::max(z_max, z);
    }

    float extent = std::max({x_max - x_min, y_max - y_min, z_max - z_min, 1e-6f});
    // Cell size: aim for ~10-20 points per cell on average
    float cell_size = extent / std::cbrt(static_cast<float>(n) / 15.0f);
    cell_size = std::max(cell_size, extent / 512.0f);  // at most 512 cells per axis

    SpatialGrid grid = SpatialGrid::build(pts, n, cell_size);

    for (size_t i = 0; i < n; ++i) {
        mean_distances[i] = grid.mean_k_distance(pts, i, k_neighbors);
    }

    // Compute global mean and standard deviation of mean distances
    double global_sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        global_sum += mean_distances[i];
    }
    double global_mean = global_sum / static_cast<double>(n);

    double var_sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double diff = mean_distances[i] - global_mean;
        var_sum += diff * diff;
    }
    double global_std = std::sqrt(var_sum / static_cast<double>(n));

    // Threshold
    float threshold = static_cast<float>(global_mean + std_ratio * global_std);

    // Collect inlier points
    std::vector<float> inliers;
    inliers.reserve(n * 3);

    for (size_t i = 0; i < n; ++i) {
        if (mean_distances[i] <= threshold) {
            inliers.push_back(pts[i * 3 + 0]);
            inliers.push_back(pts[i * 3 + 1]);
            inliers.push_back(pts[i * 3 + 2]);
        }
    }

    PointCloud result;
    result.format = PointFormat::XYZ_FLOAT;
    result.point_count = inliers.size() / 3;
    if (!inliers.empty()) {
        result.buffer = SharedBuffer::allocate(inliers.size() * sizeof(float));
        std::memcpy(result.buffer.data(), inliers.data(),
                    inliers.size() * sizeof(float));
    }

    return result;
}

HeightMap filter_height_map(
    const HeightMap& hmap,
    const std::string& type,
    int kernel_size)
{
    if (hmap.width <= 0 || hmap.height <= 0) {
        return hmap;
    }

    // Ensure odd kernel size >= 3
    int ks = std::max(3, kernel_size | 1);

    // Get the height data as a cv::Mat (CV_32F)
    cv::Mat src = hmap.to_cv_mat();
    if (src.empty()) return hmap;

    // Create a mask of valid (non-NaN) pixels
    cv::Mat valid_mask(src.rows, src.cols, CV_8U);
    cv::Mat src_clean = src.clone();

    for (int r = 0; r < src.rows; ++r) {
        float* row = src_clean.ptr<float>(r);
        uint8_t* mrow = valid_mask.ptr<uint8_t>(r);
        for (int c = 0; c < src.cols; ++c) {
            if (std::isnan(row[c])) {
                row[c] = 0.0f;
                mrow[c] = 0;
            } else {
                mrow[c] = 255;
            }
        }
    }

    cv::Mat filtered;

    if (type == "median") {
        // medianBlur requires CV_8U or CV_32F. For CV_32F, kernel size must be
        // 3 or 5. For larger kernels, we convert to higher precision workaround.
        if (ks <= 5) {
            cv::medianBlur(src_clean, filtered, ks);
        } else {
            // Fallback: use repeated 5x5 median for larger kernels
            filtered = src_clean.clone();
            int passes = (ks - 1) / 4;  // approximate
            for (int p = 0; p < std::max(1, passes); ++p) {
                cv::medianBlur(filtered, filtered, 5);
            }
        }
    } else if (type == "gaussian") {
        cv::GaussianBlur(src_clean, filtered, cv::Size(ks, ks), 0);
    } else {
        // Unknown filter type, return copy
        filtered = src_clean;
    }

    // Restore NaN in originally invalid regions
    for (int r = 0; r < filtered.rows; ++r) {
        float* frow = filtered.ptr<float>(r);
        const uint8_t* mrow = valid_mask.ptr<uint8_t>(r);
        for (int c = 0; c < filtered.cols; ++c) {
            if (mrow[c] == 0) {
                frow[c] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    }

    // Build output HeightMap
    HeightMap result = HeightMap::create(hmap.width, hmap.height, hmap.resolution_mm);
    result.origin_x = hmap.origin_x;
    result.origin_y = hmap.origin_y;

    // Copy filtered data
    std::memcpy(result.buffer.data(), filtered.data,
                hmap.width * hmap.height * sizeof(float));

    return result;
}

} // namespace vxl::detail
