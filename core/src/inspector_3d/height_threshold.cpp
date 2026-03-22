#include "vxl/inspector_3d.h"

#include <cmath>
#include <cstring>

namespace vxl {

Result<Image> Inspector3D::height_threshold(const HeightMap& hmap,
                                             float min_h, float max_h) {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<Image>::failure(ErrorCode::INVALID_PARAMETER,
                                     "HeightMap is empty");
    }

    Image mask = Image::create(hmap.width, hmap.height, PixelFormat::GRAY8);
    const float* src = reinterpret_cast<const float*>(hmap.buffer.data());
    uint8_t* dst = mask.buffer.data();

    // Zero-initialize.
    std::memset(dst, 0, static_cast<size_t>(mask.stride) * mask.height);

    for (int y = 0; y < hmap.height; ++y) {
        for (int x = 0; x < hmap.width; ++x) {
            float z = src[y * hmap.width + x];
            if (std::isnan(z)) continue;
            if (z < min_h || z > max_h) {
                dst[y * mask.stride + x] = 255;
            }
        }
    }

    return Result<Image>::success(std::move(mask));
}

} // namespace vxl
