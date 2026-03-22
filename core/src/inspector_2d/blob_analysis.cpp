#include "vxl/inspector_2d.h"

#include <cmath>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vxl {

Result<std::vector<BlobResult>> Inspector2D::blob_analysis(
    const Image& image, int threshold, int min_area, int max_area) {

    if (!image.buffer.data() || image.width <= 0 || image.height <= 0) {
        return Result<std::vector<BlobResult>>::failure(
            ErrorCode::INVALID_PARAMETER, "Input image is empty");
    }

    cv::Mat mat = image.to_cv_mat();

    // Convert to grayscale if needed.
    cv::Mat gray;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = mat;
    }

    // Binarize.
    cv::Mat binary;
    cv::threshold(gray, binary, threshold, 255, cv::THRESH_BINARY);

    // Find contours.
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<BlobResult> results;
    results.reserve(contours.size());

    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < min_area || area > max_area) continue;

        double perimeter = cv::arcLength(contour, true);
        float circularity = 0.0f;
        if (perimeter > 0.0) {
            circularity = static_cast<float>(
                4.0 * M_PI * area / (perimeter * perimeter));
        }

        cv::Rect br = cv::boundingRect(contour);
        cv::Moments m = cv::moments(contour);

        BlobResult blob;
        blob.bounding_box = ROI{br.x, br.y, br.width, br.height};
        blob.area         = static_cast<float>(area);
        blob.circularity  = circularity;
        if (m.m00 > 0.0) {
            blob.centroid.x = static_cast<float>(m.m10 / m.m00);
            blob.centroid.y = static_cast<float>(m.m01 / m.m00);
        }

        results.push_back(std::move(blob));
    }

    return Result<std::vector<BlobResult>>::success(std::move(results));
}

} // namespace vxl
