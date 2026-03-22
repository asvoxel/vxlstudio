#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/inspector_3d.h"
#include "vxl/types.h"

// Forward-declare to avoid pulling in reconstruct.h (camera/calib dependency).
namespace vxl { struct ReconstructParams; }

namespace vxl {

// ---------------------------------------------------------------------------
// Recipe -- full inspection recipe: load / save / validate / inspect
// ---------------------------------------------------------------------------
class VXL_EXPORT Recipe {
public:
    Recipe();
    ~Recipe();
    Recipe(const Recipe& other);
    Recipe& operator=(const Recipe& other);
    Recipe(Recipe&& other) noexcept;
    Recipe& operator=(Recipe&& other) noexcept;

    /// Load a recipe from a JSON file.
    static Result<Recipe> load(const std::string& path);

    /// Save this recipe to a JSON file.
    Result<void> save(const std::string& path) const;

    /// Validate that all types, ROIs, and parameters are well-formed.
    Result<void> validate() const;

    /// Run the configured 3D inspectors on the given height map.
    Result<InspectionResult> inspect(const HeightMap& hmap) const;

    // ----- accessors -----
    std::string name() const;
    std::string type() const;
    const std::vector<InspectorConfig>& inspector_configs() const;

    /// Returns the reference plane ROI (from recipe JSON "reference.ref_plane_roi").
    /// If no reference plane is configured, the returned ROI has area() == 0.
    ROI ref_plane_roi() const;

    /// Returns true if a reference plane ROI with non-zero area is configured.
    bool has_reference_plane() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace vxl
