#pragma once

#include <string>
#include <vector>

#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// InspectorResult -- outcome of a single inspector within an inspection run
// ---------------------------------------------------------------------------
struct VXL_EXPORT InspectorResult {
    std::string              name;
    std::string              type;
    std::string              severity;
    bool                     pass = true;
    std::string              message;
    MeasureResult            measure;
    std::vector<DefectRegion> defects;
};

// ---------------------------------------------------------------------------
// Helper: attach per-inspector detail to InspectionResult
// ---------------------------------------------------------------------------
/// Populate an InspectionResult from a vector of per-inspector results.
VXL_EXPORT void build_inspection_result(
    InspectionResult& out,
    const std::vector<InspectorResult>& per_inspector,
    const std::string& recipe_name);

} // namespace vxl
