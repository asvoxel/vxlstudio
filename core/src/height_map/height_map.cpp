#include "vxl/height_map.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl {

// ---------------------------------------------------------------------------
// Plane
// ---------------------------------------------------------------------------
double Plane::distance(double x, double y, double z) const {
    return a * x + b * y + c * z + d;
}

// ---------------------------------------------------------------------------
// HeightMapProcessor
// ---------------------------------------------------------------------------

// Helper: get raw float pointer from HeightMap buffer.
static const float* hmap_data(const HeightMap& hmap) {
    return reinterpret_cast<const float*>(hmap.buffer.data());
}

Result<Plane> HeightMapProcessor::fit_reference_plane(const HeightMap& hmap,
                                                       const ROI& roi) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<Plane>::failure(ErrorCode::INVALID_PARAMETER,
                                     "HeightMap is empty");
    }

    // Clamp ROI to image bounds.
    ROI r;
    r.x = std::max(0, roi.x);
    r.y = std::max(0, roi.y);
    r.w = std::min(roi.x + roi.w, hmap.width) - r.x;
    r.h = std::min(roi.y + roi.h, hmap.height) - r.y;

    if (r.w <= 0 || r.h <= 0) {
        return Result<Plane>::failure(ErrorCode::INSPECT_ROI_OUT_OF_BOUNDS,
                                     "ROI does not overlap the height map");
    }

    const float* data = hmap_data(hmap);

    // Least-squares fit: z = a*x + b*y + c
    // Solve the 3x3 normal equations:
    //   | sum(x*x)  sum(x*y)  sum(x) | | a |   | sum(x*z) |
    //   | sum(x*y)  sum(y*y)  sum(y) | | b | = | sum(y*z) |
    //   | sum(x)    sum(y)    n      | | c |   | sum(z)   |
    double sx = 0, sy = 0, sz = 0;
    double sxx = 0, syy = 0, sxy = 0;
    double sxz = 0, syz = 0;
    int n = 0;

    for (int iy = r.y; iy < r.y + r.h; ++iy) {
        for (int ix = r.x; ix < r.x + r.w; ++ix) {
            float z = data[iy * hmap.width + ix];
            if (std::isnan(z)) continue;
            double x = static_cast<double>(ix);
            double y = static_cast<double>(iy);
            double zd = static_cast<double>(z);
            sx  += x;   sy  += y;   sz  += zd;
            sxx += x*x; syy += y*y; sxy += x*y;
            sxz += x*zd; syz += y*zd;
            ++n;
        }
    }

    if (n < 3) {
        return Result<Plane>::failure(ErrorCode::CALIB_INSUFFICIENT_DATA,
                                     "Too few valid pixels for plane fit");
    }

    // Solve 3x3 system via Cramer's rule.
    double dn = static_cast<double>(n);
    // Matrix:
    //   A = | sxx  sxy  sx |
    //       | sxy  syy  sy |
    //       | sx   sy   dn |
    double det =   sxx * (syy * dn  - sy * sy)
                 - sxy * (sxy * dn  - sy * sx)
                 + sx  * (sxy * sy  - syy * sx);

    if (std::abs(det) < 1e-15) {
        return Result<Plane>::failure(ErrorCode::CALIB_CONVERGENCE_FAILED,
                                     "Degenerate plane fit (det ~0)");
    }

    double inv_det = 1.0 / det;

    double pa = ( sxz * (syy * dn - sy * sy)
                - sxy * (syz * dn - sy * sz)
                + sx  * (syz * sy - syy * sz)) * inv_det;

    double pb = ( sxx * (syz * dn - sy * sz)
                - sxz * (sxy * dn - sy * sx)
                + sx  * (sxy * sz - syz * sx)) * inv_det;

    double pc = ( sxx * (syy * sz - syz * sy)
                - sxy * (sxy * sz - syz * sx)
                + sxz * (sxy * sy - syy * sx)) * inv_det;

    // Plane: pa*x + pb*y - z + pc = 0  =>  a=pa, b=pb, c=-1, d=pc
    // Normalize so that sqrt(a^2+b^2+c^2) = 1.
    double norm = std::sqrt(pa * pa + pb * pb + 1.0);
    Plane plane;
    plane.a =  pa / norm;
    plane.b =  pb / norm;
    plane.c = -1.0 / norm;
    plane.d =  pc / norm;

    return Result<Plane>::success(plane);
}

Result<HeightMap> HeightMapProcessor::subtract_reference(const HeightMap& hmap,
                                                          const Plane& plane) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<HeightMap>::failure(ErrorCode::INVALID_PARAMETER,
                                         "HeightMap is empty");
    }

    HeightMap out = HeightMap::create(hmap.width, hmap.height, hmap.resolution_mm);
    out.origin_x = hmap.origin_x;
    out.origin_y = hmap.origin_y;

    const float* src = hmap_data(hmap);
    float* dst = reinterpret_cast<float*>(out.buffer.data());

    // The plane equation is ax + by + cz + d = 0.
    // The z-value on the plane at position (x, y) is:
    //   z_plane = -(a*x + b*y + d) / c
    // The residual after subtracting the reference is z - z_plane.
    if (std::abs(plane.c) < 1e-15) {
        return Result<HeightMap>::failure(ErrorCode::INVALID_PARAMETER,
                                         "Plane is vertical (c ~= 0)");
    }
    const double inv_c = -1.0 / plane.c;

    for (int y = 0; y < hmap.height; ++y) {
        for (int x = 0; x < hmap.width; ++x) {
            int idx = y * hmap.width + x;
            float z = src[idx];
            if (std::isnan(z)) {
                dst[idx] = std::numeric_limits<float>::quiet_NaN();
            } else {
                double z_plane = (plane.a * x + plane.b * y + plane.d) * inv_c;
                dst[idx] = z - static_cast<float>(z_plane);
            }
        }
    }

    return Result<HeightMap>::success(std::move(out));
}

Result<HeightMap> HeightMapProcessor::apply_filter(const HeightMap& hmap,
                                                    const std::string& type,
                                                    int kernel_size) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<HeightMap>::failure(ErrorCode::INVALID_PARAMETER,
                                         "HeightMap is empty");
    }
    if (kernel_size < 1 || kernel_size % 2 == 0) {
        return Result<HeightMap>::failure(ErrorCode::INVALID_PARAMETER,
                                         "kernel_size must be a positive odd integer");
    }

    cv::Mat src(hmap.height, hmap.width, CV_32F,
                const_cast<float*>(hmap_data(hmap)));
    cv::Mat dst;

    if (type == "median") {
        // cv::medianBlur requires CV_8U for kernel > 5 on some builds;
        // for float data we use a manual approach if needed, but OpenCV 4.x
        // supports CV_32F medianBlur for all kernel sizes.
        cv::medianBlur(src, dst, kernel_size);
    } else if (type == "gaussian") {
        cv::GaussianBlur(src, dst, cv::Size(kernel_size, kernel_size), 0.0);
    } else {
        return Result<HeightMap>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Unknown filter type '" + type + "'; use 'median' or 'gaussian'");
    }

    HeightMap out = HeightMap::create(hmap.width, hmap.height, hmap.resolution_mm);
    out.origin_x = hmap.origin_x;
    out.origin_y = hmap.origin_y;
    std::memcpy(out.buffer.data(), dst.data,
                static_cast<size_t>(hmap.width) * hmap.height * sizeof(float));

    return Result<HeightMap>::success(std::move(out));
}

HeightMap HeightMapProcessor::crop_roi(const HeightMap& hmap, const ROI& roi) {
    ROI r;
    r.x = std::max(0, roi.x);
    r.y = std::max(0, roi.y);
    r.w = std::min(roi.x + roi.w, hmap.width) - r.x;
    r.h = std::min(roi.y + roi.h, hmap.height) - r.y;

    if (r.w <= 0 || r.h <= 0) {
        return HeightMap{};  // empty
    }

    HeightMap out = HeightMap::create(r.w, r.h, hmap.resolution_mm);
    out.origin_x = hmap.origin_x + static_cast<float>(r.x) * hmap.resolution_mm;
    out.origin_y = hmap.origin_y + static_cast<float>(r.y) * hmap.resolution_mm;

    const float* src = hmap_data(hmap);
    float* dst = reinterpret_cast<float*>(out.buffer.data());

    for (int y = 0; y < r.h; ++y) {
        std::memcpy(dst + y * r.w,
                    src + (r.y + y) * hmap.width + r.x,
                    static_cast<size_t>(r.w) * sizeof(float));
    }

    return out;
}

Result<HeightMap> HeightMapProcessor::interpolate_holes(const HeightMap& hmap) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<HeightMap>::failure(ErrorCode::INVALID_PARAMETER,
                                         "HeightMap is empty");
    }

    const int W = hmap.width;
    const int H = hmap.height;
    const float* src = hmap_data(hmap);

    HeightMap out = HeightMap::create(W, H, hmap.resolution_mm);
    out.origin_x = hmap.origin_x;
    out.origin_y = hmap.origin_y;
    float* dst = reinterpret_cast<float*>(out.buffer.data());
    std::memcpy(dst, src, static_cast<size_t>(W) * H * sizeof(float));

    // BFS from all valid pixels to fill NaN holes with nearest valid value.
    std::vector<bool> visited(static_cast<size_t>(W) * H, false);
    std::queue<std::pair<int, int>> q;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int idx = y * W + x;
            if (!std::isnan(dst[idx])) {
                visited[idx] = true;
                // Only enqueue if it has a NaN neighbor (optimization).
                bool has_nan_neighbor = false;
                const int dx[] = {-1, 1, 0, 0};
                const int dy[] = {0, 0, -1, 1};
                for (int d = 0; d < 4; ++d) {
                    int nx = x + dx[d], ny = y + dy[d];
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                        if (std::isnan(dst[ny * W + nx])) {
                            has_nan_neighbor = true;
                            break;
                        }
                    }
                }
                if (has_nan_neighbor) {
                    q.push({x, y});
                }
            }
        }
    }

    const int dx[] = {-1, 1, 0, 0};
    const int dy[] = {0, 0, -1, 1};

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        float val = dst[cy * W + cx];
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                int nidx = ny * W + nx;
                if (!visited[nidx]) {
                    visited[nidx] = true;
                    dst[nidx] = val;
                    q.push({nx, ny});
                }
            }
        }
    }

    return Result<HeightMap>::success(std::move(out));
}

} // namespace vxl
