#include "vxl/inspector_3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl {

Result<CompareResult> Inspector3D::template_compare(
    const HeightMap& current,
    const HeightMap& reference,
    float threshold_mm,
    int min_area_pixels) {

    // Validate inputs.
    if (!current.buffer.data() || current.width <= 0 || current.height <= 0) {
        return Result<CompareResult>::failure(ErrorCode::INVALID_PARAMETER,
                                              "Current HeightMap is empty");
    }
    if (!reference.buffer.data() || reference.width <= 0 || reference.height <= 0) {
        return Result<CompareResult>::failure(ErrorCode::INVALID_PARAMETER,
                                              "Reference HeightMap is empty");
    }

    // Determine overlap region (crop to the smaller of the two).
    int w = std::min(current.width, reference.width);
    int h = std::min(current.height, reference.height);

    const float* cur_data = reinterpret_cast<const float*>(current.buffer.data());
    const float* ref_data = reinterpret_cast<const float*>(reference.buffer.data());

    // Step 1: Compute raw difference (CV_32F).
    cv::Mat diff(h, w, CV_32F, cv::Scalar(0.0f));

    int valid_count = 0;
    double abs_sum = 0.0;
    float max_abs_diff = 0.0f;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float zc = cur_data[y * current.width + x];
            float zr = ref_data[y * reference.width + x];

            // If either pixel is NaN, set diff to 0 (ignore).
            if (std::isnan(zc) || std::isnan(zr)) {
                diff.at<float>(y, x) = 0.0f;
                continue;
            }

            float d = zc - zr;
            diff.at<float>(y, x) = d;

            float ad = std::abs(d);
            abs_sum += ad;
            ++valid_count;
            if (ad > max_abs_diff) max_abs_diff = ad;
        }
    }

    // Step 2: Apply Gaussian blur to reduce noise.
    cv::Mat blurred;
    cv::GaussianBlur(diff, blurred, cv::Size(5, 5), 0.7);

    // Step 3: Threshold |blurred_diff| > threshold_mm => defect mask.
    cv::Mat abs_blurred;
    abs_blurred = cv::abs(blurred);

    cv::Mat mask_f;
    cv::threshold(abs_blurred, mask_f, threshold_mm, 255.0, cv::THRESH_BINARY);

    // Convert mask to GRAY8.
    cv::Mat mask_u8;
    mask_f.convertTo(mask_u8, CV_8U);

    // Build VXL Image from the mask.
    Image diff_mask = Image::from_cv_mat(mask_u8);

    // Step 4: Run defect_cluster on the mask.
    float resolution = current.resolution_mm;
    auto cluster_res = defect_cluster(diff_mask, resolution, min_area_pixels);

    CompareResult result;
    result.diff_mask = std::move(diff_mask);
    result.max_diff  = max_abs_diff;
    result.mean_diff = (valid_count > 0)
        ? static_cast<float>(abs_sum / valid_count)
        : 0.0f;

    if (cluster_res.ok()) {
        result.defects = std::move(cluster_res.value);
    }

    return Result<CompareResult>::success(std::move(result));
}

} // namespace vxl
