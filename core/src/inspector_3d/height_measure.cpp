#include "vxl/inspector_3d.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vxl {

Result<MeasureResult> Inspector3D::height_measure(const HeightMap& hmap,
                                                   const ROI& roi,
                                                   double ref_height) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<MeasureResult>::failure(ErrorCode::INVALID_PARAMETER,
                                              "HeightMap is empty");
    }

    // Clamp ROI.
    int rx = std::max(0, roi.x);
    int ry = std::max(0, roi.y);
    int rw = std::min(roi.x + roi.w, hmap.width)  - rx;
    int rh = std::min(roi.y + roi.h, hmap.height) - ry;

    if (rw <= 0 || rh <= 0) {
        return Result<MeasureResult>::failure(ErrorCode::INSPECT_ROI_OUT_OF_BOUNDS,
                                              "ROI is out of bounds");
    }

    const float* data = reinterpret_cast<const float*>(hmap.buffer.data());
    const float ref = static_cast<float>(ref_height);

    double sum  = 0.0;
    double sum2 = 0.0;
    double volume = 0.0;
    float mn = std::numeric_limits<float>::max();
    float mx = std::numeric_limits<float>::lowest();
    int n = 0;

    for (int y = ry; y < ry + rh; ++y) {
        for (int x = rx; x < rx + rw; ++x) {
            float z = data[y * hmap.width + x];
            if (std::isnan(z)) continue;
            float h = z - ref;
            if (h < mn) mn = h;
            if (h > mx) mx = h;
            sum  += h;
            sum2 += static_cast<double>(h) * h;
            if (h > 0.0f) {
                volume += h;
            }
            ++n;
        }
    }

    if (n == 0) {
        return Result<MeasureResult>::failure(ErrorCode::CALIB_INSUFFICIENT_DATA,
                                              "No valid pixels in ROI");
    }

    double avg = sum / n;
    double var = sum2 / n - avg * avg;
    if (var < 0.0) var = 0.0;

    float pixel_area = hmap.resolution_mm * hmap.resolution_mm;

    MeasureResult mr;
    mr.min_height = mn;
    mr.max_height = mx;
    mr.avg_height = static_cast<float>(avg);
    mr.std_height = static_cast<float>(std::sqrt(var));
    mr.volume     = static_cast<float>(volume * pixel_area);

    return Result<MeasureResult>::success(mr);
}

} // namespace vxl
