#pragma once

#include "vxl/export.h"
#include "vxl/error.h"
#include "vxl/types.h"

#include <memory>
#include <string>
#include <vector>

namespace vxl {

struct VXL_EXPORT GraspPose {
    Pose6D pose;
    float score = 0.0f;
    float width_mm = 0.0f;    // gripper opening width
    std::string label;
};

struct VXL_EXPORT GuidanceParams {
    std::string strategy = "top_down";  // "top_down", "side", "custom"
    float approach_distance_mm = 50.0f;
    float min_score = 0.3f;
    int max_results = 10;
    float object_height_mm = 0.0f;  // 0 = auto detect
};

class VXL_EXPORT GuidanceEngine {
public:
    GuidanceEngine();
    ~GuidanceEngine();

    // Compute grasp poses from point cloud
    Result<std::vector<GraspPose>> compute_grasp(
        const PointCloud& cloud,
        const GuidanceParams& params = {});

    // Compute grasp from height map (simpler 2.5D approach)
    Result<std::vector<GraspPose>> compute_grasp_2d(
        const HeightMap& hmap,
        const GuidanceParams& params = {});

    // Simple pick point: find highest point in ROI
    static Result<GraspPose> find_pick_point(
        const HeightMap& hmap, const ROI& roi);

    // Coordinate transform: camera frame -> robot frame
    static Result<Pose6D> camera_to_robot(
        const Pose6D& camera_pose,
        const Pose6D& hand_eye_transform);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
