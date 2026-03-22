#include "vxl/visual_ai.h"
#include "yolo_common.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace vxl {

// ---------------------------------------------------------------------------
// YoloSegmenter::Impl
// ---------------------------------------------------------------------------
struct YoloSegmenter::Impl {
    Inference engine;
    std::vector<std::string> names;
    int input_size = 640;
    int num_classes = 0;
    int num_mask_coeffs = 32;  // YOLO-seg default: 32 mask prototype coefficients
};

YoloSegmenter::YoloSegmenter() : impl_(std::make_unique<Impl>()) {}
YoloSegmenter::~YoloSegmenter() = default;

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------
Result<std::unique_ptr<YoloSegmenter>> YoloSegmenter::create(
    const std::string& onnx_path,
    const std::vector<std::string>& class_names,
    int input_size)
{
    if (onnx_path.empty()) {
        return Result<std::unique_ptr<YoloSegmenter>>::failure(
            ErrorCode::INVALID_PARAMETER, "Model path is empty");
    }

    {
        std::ifstream f(onnx_path);
        if (!f.good()) {
            return Result<std::unique_ptr<YoloSegmenter>>::failure(
                ErrorCode::FILE_NOT_FOUND, "Model file not found: " + onnx_path);
        }
    }

    auto load_result = Inference::load(onnx_path);
    if (!load_result.ok()) {
        return Result<std::unique_ptr<YoloSegmenter>>::failure(
            load_result.code, load_result.message);
    }

    std::unique_ptr<YoloSegmenter> seg(new YoloSegmenter());
    seg->impl_->engine = std::move(load_result.value);
    seg->impl_->names = class_names;
    seg->impl_->input_size = input_size;

    // YOLO-seg output0 shape: [1, 4+num_classes+num_mask_coeffs, num_predictions]
    // output1 shape: [1, num_mask_coeffs, mask_h, mask_w] (prototypes)
    auto out_shape = seg->impl_->engine.output_shape();
    if (out_shape.size() == 3 && out_shape[1] > 4) {
        // num_rows = 4 + nc + nm (nm is typically 32)
        int nm = 32;
        int nc = out_shape[1] - 4 - nm;
        if (nc > 0) {
            seg->impl_->num_classes = nc;
            seg->impl_->num_mask_coeffs = nm;
        } else {
            // Fallback: assume no mask coefficients in output0 metadata
            // (they might be inferred differently)
            seg->impl_->num_classes = out_shape[1] - 4 - 32;
            if (seg->impl_->num_classes <= 0) {
                seg->impl_->num_classes = class_names.empty() ? 80 : static_cast<int>(class_names.size());
            }
        }
    } else if (!class_names.empty()) {
        seg->impl_->num_classes = static_cast<int>(class_names.size());
    } else {
        seg->impl_->num_classes = 80;
    }

    return Result<std::unique_ptr<YoloSegmenter>>::success(std::move(seg));
}

std::string YoloSegmenter::name() const {
    return "YoloSegmenter";
}

std::vector<std::string> YoloSegmenter::class_names() const {
    return impl_->names;
}

// ---------------------------------------------------------------------------
// segment
// ---------------------------------------------------------------------------
Result<SegmentResult> YoloSegmenter::segment(const Image& image, float conf_threshold) {
    if (!image.buffer.data() || image.width <= 0 || image.height <= 0) {
        return Result<SegmentResult>::failure(
            ErrorCode::INVALID_PARAMETER, "Invalid input image");
    }

    const int input_size = impl_->input_size;
    const float iou_threshold = 0.45f;

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

    // NOTE: YOLO-seg models produce TWO outputs:
    //   output0: [1, 4+nc+nm, num_preds]  -- detections + mask coefficients
    //   output1: [1, nm, mask_h, mask_w]   -- mask prototypes
    // The Inference::run() only returns the first output as a flat vector.
    // For a full implementation, we would need multi-output support in Inference.
    // Here we use the single-output path for detection and generate a combined
    // mask from the detections. This is a limitation of the current Inference API.
    auto run_result = impl_->engine.run(resized);
    if (!run_result.ok()) {
        return Result<SegmentResult>::failure(run_result.code, run_result.message);
    }

    const auto& output = run_result.value;
    if (output.empty()) {
        return Result<SegmentResult>::failure(
            ErrorCode::INTERNAL_ERROR, "Model produced empty output");
    }

    const int nc = impl_->num_classes;
    const int nm = impl_->num_mask_coeffs;
    auto out_shape = impl_->engine.output_shape();

    int num_rows = 4 + nc + nm;
    int num_preds = 0;

    if (out_shape.size() == 3) {
        num_rows = out_shape[1];
        num_preds = out_shape[2];
    } else {
        num_preds = static_cast<int>(output.size()) / num_rows;
    }

    if (num_preds <= 0 || num_rows < 5) {
        SegmentResult empty;
        empty.num_instances = 0;
        return Result<SegmentResult>::success(std::move(empty));
    }

    // Parse detections with mask coefficients
    std::vector<yolo::DetCandidate> candidates;

    for (int p = 0; p < num_preds; ++p) {
        float cx = output[static_cast<size_t>(0 * num_preds + p)];
        float cy = output[static_cast<size_t>(1 * num_preds + p)];
        float w  = output[static_cast<size_t>(2 * num_preds + p)];
        float h  = output[static_cast<size_t>(3 * num_preds + p)];

        // Find best class (classes are at rows 4..4+nc-1)
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

        // Extract mask coefficients (rows 4+nc .. 4+nc+nm-1)
        cand.mask_coeffs.resize(static_cast<size_t>(nm));
        for (int m_idx = 0; m_idx < nm; ++m_idx) {
            int row = 4 + nc + m_idx;
            if (row < num_rows) {
                cand.mask_coeffs[static_cast<size_t>(m_idx)] =
                    output[static_cast<size_t>(row * num_preds + p)];
            }
        }

        candidates.push_back(std::move(cand));
    }

    // NMS
    auto nms_result = yolo::nms(candidates, iou_threshold);

    // Build SegmentResult
    SegmentResult seg_result;

    // Create a combined mask at original image resolution (GRAY8)
    // Each pixel gets the class_id+1 of the last instance covering it (0 = background)
    seg_result.mask = Image::create(image.width, image.height, PixelFormat::GRAY8);
    std::memset(seg_result.mask.buffer.data(), 0,
                static_cast<size_t>(image.width * image.height));

    seg_result.instances.reserve(nms_result.size());

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

        // Without the second output (mask prototypes), we approximate the mask
        // by filling the bounding box region in the combined mask.
        // In a full implementation with multi-output Inference support, we would:
        //   1. Multiply mask_coeffs x prototype_masks to get per-instance mask
        //   2. Sigmoid + threshold the result
        //   3. Resize to original coords and crop to bbox
        uint8_t* mask_data = seg_result.mask.buffer.data();
        uint8_t label = static_cast<uint8_t>(std::min(255, c.class_id + 1));
        for (int y = det.bbox.y; y < det.bbox.y + det.bbox.h && y < image.height; ++y) {
            for (int x = det.bbox.x; x < det.bbox.x + det.bbox.w && x < image.width; ++x) {
                if (x >= 0 && y >= 0) {
                    mask_data[y * image.width + x] = label;
                }
            }
        }

        seg_result.instances.push_back(std::move(det));
    }

    seg_result.num_instances = static_cast<int>(seg_result.instances.size());

    return Result<SegmentResult>::success(std::move(seg_result));
}

} // namespace vxl
