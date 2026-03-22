#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/height_map.h"
#include "vxl/result.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// InspectorConfig -- describes one 3D inspection step
// ---------------------------------------------------------------------------
struct VXL_EXPORT InspectorConfig {
    std::string                    name;
    std::string                    type;       // "height_measure", "flatness",
                                               // "height_threshold", "defect_cluster",
                                               // "ref_plane_fit", "coplanarity",
                                               // "template_compare"
    std::vector<ROI>               rois;
    std::map<std::string, double>  params;     // e.g. "min_height_mm": 0.05
    std::string                    severity = "critical"; // critical | warning | minor
};

// ---------------------------------------------------------------------------
// CompareResult -- output of template_compare
// ---------------------------------------------------------------------------
struct VXL_EXPORT CompareResult {
    Image                     diff_mask;
    std::vector<DefectRegion> defects;
    float                     max_diff  = 0.0f;
    float                     mean_diff = 0.0f;
};

// ---------------------------------------------------------------------------
// Inspector3D -- configurable 3D inspection engine
// ---------------------------------------------------------------------------
class VXL_EXPORT Inspector3D {
public:
    Inspector3D();
    ~Inspector3D();

    /// Set a reference height map (optional; used by some algorithms).
    void set_reference(const HeightMap& ref_hmap);

    /// Add an inspector configuration.
    void add_inspector(const InspectorConfig& config);

    /// Remove all inspectors.
    void clear();

    /// Run all configured inspectors on the given height map.
    Result<InspectionResult> run(const HeightMap& hmap) const;

    // ----- standalone algorithm functions -----

    /// Fit a reference plane in the given ROI.
    static Result<Plane> ref_plane_fit(const HeightMap& hmap, const ROI& roi);

    /// Compute height statistics in the ROI.
    static Result<MeasureResult> height_measure(const HeightMap& hmap,
                                                 const ROI& roi,
                                                 double ref_height = 0.0);

    /// Compute flatness (max deviation from best-fit plane) in the ROI.
    static Result<float> flatness(const HeightMap& hmap, const ROI& roi);

    /// Binary threshold on height values.
    static Result<Image> height_threshold(const HeightMap& hmap,
                                           float min_h, float max_h);

    /// Connected-component clustering on a binary mask.
    static Result<std::vector<DefectRegion>> defect_cluster(
        const Image& binary_mask,
        float resolution_mm,
        int min_area_pixels = 10);

    /// Coplanarity: measure max deviation from best-fit plane across ROI centers.
    static Result<float> coplanarity(const HeightMap& hmap,
                                     const std::vector<ROI>& rois);

    /// Template compare: diff current vs reference height map, find defects.
    static Result<CompareResult> template_compare(
        const HeightMap& current,
        const HeightMap& reference,
        float threshold_mm = 0.1f,
        int min_area_pixels = 10);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
