#include "vxl/inspector_2d.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl {

Result<Image> Inspector2D::edge_detect(
    const Image& image, double low_threshold, double high_threshold) {

    if (!image.buffer.data() || image.width <= 0 || image.height <= 0) {
        return Result<Image>::failure(ErrorCode::INVALID_PARAMETER,
                                     "Input image is empty");
    }

    cv::Mat mat = image.to_cv_mat();

    // Convert to grayscale if needed.
    cv::Mat gray;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = mat;
    }

    // Gaussian blur to reduce noise.
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(3, 3), 0);

    // Canny edge detection.
    cv::Mat edges;
    cv::Canny(blurred, edges, low_threshold, high_threshold);

    // Convert back to vxl::Image (GRAY8).
    Image result = Image::from_cv_mat(edges);

    return Result<Image>::success(std::move(result));
}

} // namespace vxl
