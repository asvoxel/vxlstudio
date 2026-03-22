#include "vxl/inspector_3d.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl {

Result<std::vector<DefectRegion>> Inspector3D::defect_cluster(
    const Image& binary_mask,
    float resolution_mm,
    int min_area_pixels) {

    if (!binary_mask.buffer.data() ||
        binary_mask.width <= 0 || binary_mask.height <= 0) {
        return Result<std::vector<DefectRegion>>::failure(
            ErrorCode::INVALID_PARAMETER, "Binary mask is empty");
    }

    cv::Mat mask = binary_mask.to_cv_mat();

    cv::Mat labels, stats, centroids;
    int n_labels = cv::connectedComponentsWithStats(mask, labels, stats,
                                                     centroids, 8, CV_32S);

    std::vector<DefectRegion> regions;
    float pixel_area = resolution_mm * resolution_mm;

    // Label 0 is background; skip it.
    for (int i = 1; i < n_labels; ++i) {
        int area_px = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area_px < min_area_pixels) continue;

        DefectRegion dr;
        dr.bounding_box.x = stats.at<int>(i, cv::CC_STAT_LEFT);
        dr.bounding_box.y = stats.at<int>(i, cv::CC_STAT_TOP);
        dr.bounding_box.w = stats.at<int>(i, cv::CC_STAT_WIDTH);
        dr.bounding_box.h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        dr.area_mm2       = static_cast<float>(area_px) * pixel_area;
        dr.centroid.x     = static_cast<float>(centroids.at<double>(i, 0));
        dr.centroid.y     = static_cast<float>(centroids.at<double>(i, 1));
        dr.max_height     = 0.0f;
        dr.avg_height     = 0.0f;
        dr.type           = "defect";

        regions.push_back(std::move(dr));
    }

    return Result<std::vector<DefectRegion>>::success(std::move(regions));
}

} // namespace vxl
