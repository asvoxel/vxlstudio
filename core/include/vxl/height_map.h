#pragma once

#include <string>

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// Plane -- 3D plane equation ax + by + cz + d = 0 (normalized)
// ---------------------------------------------------------------------------
struct VXL_EXPORT Plane {
    double a = 0.0, b = 0.0, c = 1.0, d = 0.0;

    /// Signed distance from point (x, y, z) to this plane.
    double distance(double x, double y, double z) const;
};

// ---------------------------------------------------------------------------
// HeightMapProcessor -- static utilities for height map manipulation
// ---------------------------------------------------------------------------
class VXL_EXPORT HeightMapProcessor {
public:
    /// Fit a reference plane to non-NaN pixels within the ROI using
    /// least-squares linear regression (z = a*x + b*y + c).
    static Result<Plane> fit_reference_plane(const HeightMap& hmap, const ROI& roi);

    /// Subtract reference plane from every pixel:
    ///   new_z = old_z - plane.distance(x, y, 0)
    static Result<HeightMap> subtract_reference(const HeightMap& hmap, const Plane& plane);

    /// Apply spatial filter ("median" or "gaussian") with given kernel size.
    static Result<HeightMap> apply_filter(const HeightMap& hmap, const std::string& type, int kernel_size);

    /// Extract sub-region defined by ROI. Clamps ROI to image bounds.
    static HeightMap crop_roi(const HeightMap& hmap, const ROI& roi);

    /// Fill NaN pixels using nearest valid neighbor (simple BFS).
    static Result<HeightMap> interpolate_holes(const HeightMap& hmap);
};

} // namespace vxl
