#include "vxl/visual_ai.h"
#include "yolo_common.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace vxl {

// ---------------------------------------------------------------------------
// YoloDetector::Impl
// ---------------------------------------------------------------------------
struct YoloDetector::Impl {
    Inference engine;
    std::vector<std::string> names;
    int input_size = 640;
    int num_classes = 0;
};

YoloDetector::YoloDetector() : impl_(std::make_unique<Impl>()) {}
YoloDetector::~YoloDetector() = default;

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------
Result<std::unique_ptr<YoloDetector>> YoloDetector::create(
    const std::string& onnx_path,
    const std::vector<std::string>& class_names,
    int input_size)
{
    if (onnx_path.empty()) {
        return Result<std::unique_ptr<YoloDetector>>::failure(
            ErrorCode::INVALID_PARAMETER, "Model path is empty");
    }

    {
        std::ifstream f(onnx_path);
        if (!f.good()) {
            return Result<std::unique_ptr<YoloDetector>>::failure(
                ErrorCode::FILE_NOT_FOUND, "Model file not found: " + onnx_path);
        }
    }

    auto load_result = Inference::load(onnx_path);
    if (!load_result.ok()) {
        return Result<std::unique_ptr<YoloDetector>>::failure(
            load_result.code, load_result.message);
    }

    std::unique_ptr<YoloDetector> det(new YoloDetector());
    det->impl_->engine = std::move(load_result.value);
    det->impl_->names = class_names;
    det->impl_->input_size = input_size;

    // Determine num_classes from output shape.
    // YOLOv8/v11/v26 output shape: [1, 4+num_classes, num_predictions]
    auto out_shape = det->impl_->engine.output_shape();
    if (out_shape.size() == 3 && out_shape[1] > 4) {
        det->impl_->num_classes = out_shape[1] - 4;
    } else if (!class_names.empty()) {
        det->impl_->num_classes = static_cast<int>(class_names.size());
    } else {
        det->impl_->num_classes = 80;  // COCO default
    }

    return Result<std::unique_ptr<YoloDetector>>::success(std::move(det));
}

std::string YoloDetector::name() const {
    return "YoloDetector";
}

std::vector<std::string> YoloDetector::class_names() const {
    return impl_->names;
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------
Result<DetectionResult> YoloDetector::detect(
    const Image& image, float conf_threshold, float iou_threshold)
{
    if (!image.buffer.data() || image.width <= 0 || image.height <= 0) {
        return Result<DetectionResult>::failure(
            ErrorCode::INVALID_PARAMETER, "Invalid input image");
    }

    const int input_size = impl_->input_size;

    // Preprocess with letterbox
    auto [tensor, lb_info] = yolo::preprocess_yolo(image, input_size);

    // Build an RGB8 image for Inference::run()
    Image resized = Image::create(input_size, input_size, PixelFormat::RGB8);
    uint8_t* dst = resized.buffer.data();
    for (int y = 0; y < input_size; ++y) {
        for (int x = 0; x < input_size; ++x) {
            size_t base = static_cast<size_t>(y * input_size + x);
            float r = tensor[0 * input_size * input_size + base];
            float g = tensor[1 * input_size * input_size + base];
            float b = tensor[2 * input_size * input_size + base];
            dst[(y * input_size + x) * 3 + 0] = static_cast<uint8_t>(std::min(255.0f, r * 255.0f));
            dst[(y * input_size + x) * 3 + 1] = static_cast<uint8_t>(std::min(255.0f, g * 255.0f));
            dst[(y * input_size + x) * 3 + 2] = static_cast<uint8_t>(std::min(255.0f, b * 255.0f));
        }
    }

    auto run_result = impl_->engine.run(resized);
    if (!run_result.ok()) {
        return Result<DetectionResult>::failure(run_result.code, run_result.message);
    }

    const auto& output = run_result.value;
    if (output.empty()) {
        return Result<DetectionResult>::failure(
            ErrorCode::INTERNAL_ERROR, "Model produced empty output");
    }

    // Parse YOLO output: [1, 4+num_classes, num_predictions]
    // Raw output is flattened as: for each of (4+nc) rows, num_preds columns
    const int nc = impl_->num_classes;
    auto out_shape = impl_->engine.output_shape();

    int num_rows = 4 + nc;
    int num_preds = 0;

    if (out_shape.size() == 3) {
        num_rows = out_shape[1];
        num_preds = out_shape[2];
    } else {
        // Fallback: try to infer
        num_preds = static_cast<int>(output.size()) / num_rows;
    }

    if (num_preds <= 0 || num_rows < 5) {
        DetectionResult empty_result;
        empty_result.total_count = 0;
        return Result<DetectionResult>::success(std::move(empty_result));
    }

    // Transpose and filter: output layout is [num_rows, num_preds]
    // Row 0: cx, Row 1: cy, Row 2: w, Row 3: h, Row 4..4+nc-1: class scores
    std::vector<yolo::DetCandidate> candidates;

    for (int p = 0; p < num_preds; ++p) {
        float cx = output[static_cast<size_t>(0 * num_preds + p)];
        float cy = output[static_cast<size_t>(1 * num_preds + p)];
        float w  = output[static_cast<size_t>(2 * num_preds + p)];
        float h  = output[static_cast<size_t>(3 * num_preds + p)];

        // Find best class
        int best_class = 0;
        float best_score = output[static_cast<size_t>(4 * num_preds + p)];
        for (int c = 1; c < nc; ++c) {
            float s = output[static_cast<size_t>((4 + c) * num_preds + p)];
            if (s > best_score) {
                best_score = s;
                best_class = c;
            }
        }

        if (best_score < conf_threshold) continue;

        float x1, y1, x2, y2;
        yolo::xywh_to_xyxy(cx, cy, w, h, x1, y1, x2, y2);

        yolo::DetCandidate cand;
        cand.x1 = x1;
        cand.y1 = y1;
        cand.x2 = x2;
        cand.y2 = y2;
        cand.confidence = best_score;
        cand.class_id = best_class;
        candidates.push_back(cand);
    }

    // NMS
    auto nms_result = yolo::nms(candidates, iou_threshold);

    // Build DetectionResult
    DetectionResult det_result;
    det_result.detections.reserve(nms_result.size());

    for (const auto& c : nms_result) {
        Detection det;
        det.bbox = yolo::letterbox_to_original(c.x1, c.y1, c.x2, c.y2, lb_info);
        det.class_id = c.class_id;
        det.confidence = c.confidence;

        if (c.class_id < static_cast<int>(impl_->names.size())) {
            det.class_name = impl_->names[static_cast<size_t>(c.class_id)];
        } else {
            det.class_name = "class_" + std::to_string(c.class_id);
        }

        det_result.detections.push_back(std::move(det));
    }

    det_result.total_count = static_cast<int>(det_result.detections.size());

    return Result<DetectionResult>::success(std::move(det_result));
}

} // namespace vxl
