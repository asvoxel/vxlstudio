#include "vxl/inspector_3d.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace vxl {

Result<float> Inspector3D::coplanarity(const HeightMap& hmap,
                                       const std::vector<ROI>& rois) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<float>::failure(ErrorCode::INVALID_PARAMETER,
                                     "HeightMap is empty");
    }

    if (rois.size() < 3) {
        return Result<float>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Coplanarity requires at least 3 ROIs for plane fitting");
    }

    const float* data = reinterpret_cast<const float*>(hmap.buffer.data());

    // Step 1: For each ROI, compute center coordinates and average height.
    struct RoiPoint {
        double cx;  // x center in pixel coordinates
        double cy;  // y center in pixel coordinates
        double avg_z;
    };

    std::vector<RoiPoint> points;
    points.reserve(rois.size());

    for (const auto& roi : rois) {
        // Clamp ROI to image bounds.
        int rx = std::max(0, roi.x);
        int ry = std::max(0, roi.y);
        int rw = std::min(roi.x + roi.w, hmap.width)  - rx;
        int rh = std::min(roi.y + roi.h, hmap.height) - ry;

        if (rw <= 0 || rh <= 0) continue;

        double sum = 0.0;
        int n = 0;

        for (int y = ry; y < ry + rh; ++y) {
            for (int x = rx; x < rx + rw; ++x) {
                float z = data[y * hmap.width + x];
                if (std::isnan(z)) continue;
                sum += z;
                ++n;
            }
        }

        if (n == 0) continue;  // skip all-NaN ROI

        double avg_z = sum / n;
        double cx = rx + rw / 2.0;
        double cy = ry + rh / 2.0;

        points.push_back({cx, cy, avg_z});
    }

    if (points.size() < 3) {
        return Result<float>::failure(
            ErrorCode::CALIB_INSUFFICIENT_DATA,
            "Fewer than 3 ROIs with valid data after NaN filtering");
    }

    // Step 2: Fit best plane z = a*x + b*y + c  through all ROI center points
    //         using least-squares (normal equations).
    //
    //         | sum(x^2)  sum(xy)   sum(x)  |   |a|   | sum(xz) |
    //         | sum(xy)   sum(y^2)  sum(y)  | * |b| = | sum(yz) |
    //         | sum(x)    sum(y)    N       |   |c|   | sum(z)  |

    const int N = static_cast<int>(points.size());
    double sx = 0, sy = 0, sz = 0;
    double sxx = 0, syy = 0, sxy = 0;
    double sxz = 0, syz = 0;

    for (const auto& p : points) {
        sx  += p.cx;
        sy  += p.cy;
        sz  += p.avg_z;
        sxx += p.cx * p.cx;
        syy += p.cy * p.cy;
        sxy += p.cx * p.cy;
        sxz += p.cx * p.avg_z;
        syz += p.cy * p.avg_z;
    }

    // Solve 3x3 system via Cramer's rule.
    // Matrix:
    //   [sxx, sxy, sx ]
    //   [sxy, syy, sy ]
    //   [sx,  sy,  N  ]

    auto det3 = [](double a00, double a01, double a02,
                   double a10, double a11, double a12,
                   double a20, double a21, double a22) {
        return a00 * (a11 * a22 - a12 * a21)
             - a01 * (a10 * a22 - a12 * a20)
             + a02 * (a10 * a21 - a11 * a20);
    };

    double D = det3(sxx, sxy, sx,
                    sxy, syy, sy,
                    sx,  sy,  N);

    if (std::abs(D) < 1e-15) {
        // Degenerate case (all points collinear or coincident).
        // Fall back to simple max-min deviation.
        double min_z = points[0].avg_z, max_z = points[0].avg_z;
        for (const auto& p : points) {
            min_z = std::min(min_z, p.avg_z);
            max_z = std::max(max_z, p.avg_z);
        }
        return Result<float>::success(static_cast<float>(max_z - min_z));
    }

    double Da = det3(sxz, sxy, sx,
                     syz, syy, sy,
                     sz,  sy,  N);
    double Db = det3(sxx, sxz, sx,
                     sxy, syz, sy,
                     sx,  sz,  N);
    double Dc = det3(sxx, sxy, sxz,
                     sxy, syy, syz,
                     sx,  sy,  sz);

    double a = Da / D;
    double b = Db / D;
    double c = Dc / D;

    // Step 3: Compute max deviation of any ROI average height from the plane.
    double max_dev = 0.0;

    for (const auto& p : points) {
        double z_plane = a * p.cx + b * p.cy + c;
        double dev = std::abs(p.avg_z - z_plane);
        if (dev > max_dev) max_dev = dev;
    }

    return Result<float>::success(static_cast<float>(max_dev));
}

} // namespace vxl
