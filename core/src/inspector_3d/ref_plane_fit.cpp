#include "vxl/inspector_3d.h"

#include "vxl/height_map.h"

namespace vxl {

Result<Plane> Inspector3D::ref_plane_fit(const HeightMap& hmap, const ROI& roi) {
    return HeightMapProcessor::fit_reference_plane(hmap, roi);
}

} // namespace vxl
