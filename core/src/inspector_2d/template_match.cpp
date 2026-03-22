#include "vxl/inspector_2d.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vxl {

Result<MatchResult> Inspector2D::template_match(
    const Image& image, const Image& templ, float min_score) {

    if (!image.buffer.data() || image.width <= 0 || image.height <= 0) {
        return Result<MatchResult>::failure(ErrorCode::INVALID_PARAMETER,
                                           "Input image is empty");
    }
    if (!templ.buffer.data() || templ.width <= 0 || templ.height <= 0) {
        return Result<MatchResult>::failure(ErrorCode::INVALID_PARAMETER,
                                           "Template image is empty");
    }
    if (templ.width > image.width || templ.height > image.height) {
        return Result<MatchResult>::failure(ErrorCode::INVALID_PARAMETER,
                                           "Template is larger than image");
    }

    cv::Mat img_mat = image.to_cv_mat();
    cv::Mat tpl_mat = templ.to_cv_mat();

    // Convert to grayscale if needed.
    cv::Mat img_gray, tpl_gray;
    if (img_mat.channels() == 3) {
        cv::cvtColor(img_mat, img_gray, cv::COLOR_BGR2GRAY);
    } else {
        img_gray = img_mat;
    }
    if (tpl_mat.channels() == 3) {
        cv::cvtColor(tpl_mat, tpl_gray, cv::COLOR_BGR2GRAY);
    } else {
        tpl_gray = tpl_mat;
    }

    // Try matching at a few rotation angles; pick best score.
    float best_score = -1.0f;
    float best_x = 0.0f;
    float best_y = 0.0f;
    float best_angle = 0.0f;

    const float angles[] = {0.0f, 90.0f, 180.0f, 270.0f};

    for (float angle : angles) {
        cv::Mat rotated_tpl;
        if (angle == 0.0f) {
            rotated_tpl = tpl_gray;
        } else {
            cv::Point2f center(tpl_gray.cols / 2.0f, tpl_gray.rows / 2.0f);
            cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);
            cv::warpAffine(tpl_gray, rotated_tpl, rot, tpl_gray.size());
        }

        // Skip if rotated template is larger than image.
        if (rotated_tpl.cols > img_gray.cols || rotated_tpl.rows > img_gray.rows) {
            continue;
        }

        cv::Mat result;
        cv::matchTemplate(img_gray, rotated_tpl, result, cv::TM_CCOEFF_NORMED);

        double min_val, max_val;
        cv::Point min_loc, max_loc;
        cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);

        if (static_cast<float>(max_val) > best_score) {
            best_score = static_cast<float>(max_val);
            best_x = static_cast<float>(max_loc.x);
            best_y = static_cast<float>(max_loc.y);
            best_angle = angle;
        }
    }

    MatchResult mr;
    mr.x     = best_x;
    mr.y     = best_y;
    mr.angle = best_angle;
    mr.score = best_score;
    mr.found = (best_score >= min_score);

    return Result<MatchResult>::success(std::move(mr));
}

} // namespace vxl
