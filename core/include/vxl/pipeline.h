#pragma once

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vxl/calibration.h"
#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// StepType -- identifies a pipeline processing stage
// ---------------------------------------------------------------------------
enum class StepType {
    CAPTURE,        // Acquire frames from camera
    RECONSTRUCT,    // Frames -> HeightMap / PointCloud
    INSPECT_3D,     // 3D inspection (Recipe-driven)
    INSPECT_2D,     // 2D inspection operators
    OUTPUT,         // IO output + logging
    CUSTOM          // User-defined callback
};

// ---------------------------------------------------------------------------
// PipelineStep -- configuration for one step in the pipeline
// ---------------------------------------------------------------------------
struct VXL_EXPORT PipelineStep {
    StepType    type = StepType::CUSTOM;
    std::string name;
    std::unordered_map<std::string, std::string> params;
};

// ---------------------------------------------------------------------------
// PipelineContext -- data flowing between pipeline steps
// ---------------------------------------------------------------------------
struct VXL_EXPORT PipelineContext {
    std::vector<Image>  frames;
    HeightMap           height_map;
    PointCloud          point_cloud;
    InspectionResult    result;
    std::unordered_map<std::string, std::any> custom_data;
};

// Callback signature for CUSTOM step type
using CustomStepCallback = std::function<Result<void>(PipelineContext&)>;

// ---------------------------------------------------------------------------
// Pipeline -- linear executor that runs a sequence of steps
// ---------------------------------------------------------------------------
class VXL_EXPORT Pipeline {
public:
    /// Load a pipeline definition from a JSON file.
    static Result<Pipeline> load(const std::string& path);

    /// Construct an empty pipeline (build programmatically).
    Pipeline();

    /// Append a step to the pipeline.
    void add_step(const PipelineStep& step);

    /// Register a callback for a CUSTOM step identified by name.
    void set_custom_callback(const std::string& step_name, CustomStepCallback cb);

    // -- configuration --------------------------------------------------------

    /// Set the camera device id used by CAPTURE steps.
    void set_camera_id(const std::string& id);

    /// Set calibration parameters used by RECONSTRUCT steps.
    void set_calibration(const CalibrationParams& calib);

    /// Set the recipe JSON path used by INSPECT_3D steps.
    void set_recipe(const std::string& recipe_path);

    // -- execution ------------------------------------------------------------

    /// Execute all steps sequentially and return the final context.
    Result<PipelineContext> run_once();

    /// Start continuous execution in a background thread.
    /// @p on_complete is invoked after every successful cycle.
    void start(std::function<void(const PipelineContext&)> on_complete);

    /// Stop the continuous execution loop (blocks until the worker exits).
    void stop();

    /// Returns true while the background loop is running.
    bool is_running() const;

    // -- introspection --------------------------------------------------------

    /// Return the ordered list of steps.
    const std::vector<PipelineStep>& steps() const;

    /// Serialize the pipeline definition to a JSON file.
    Result<void> save(const std::string& path) const;

    ~Pipeline();
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
