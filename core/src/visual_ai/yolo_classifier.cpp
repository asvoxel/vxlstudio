#include "vxl/visual_ai.h"
#include "yolo_common.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

namespace vxl {

// ---------------------------------------------------------------------------
// YoloClassifier::Impl
// ---------------------------------------------------------------------------
struct YoloClassifier::Impl {
    Inference engine;
    std::vector<std::string> names;
    int input_size = 224;  // YOLO-cls default
};

YoloClassifier::YoloClassifier() : impl_(std::make_unique<Impl>()) {}
YoloClassifier::~YoloClassifier() = default;

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------
Result<std::unique_ptr<YoloClassifier>> YoloClassifier::create(
    const std::string& onnx_path,
    const std::vector<std::string>& class_names)
{
    if (onnx_path.empty()) {
        return Result<std::unique_ptr<YoloClassifier>>::failure(
            ErrorCode::INVALID_PARAMETER, "Model path is empty");
    }

    {
        std::ifstream f(onnx_path);
        if (!f.good()) {
            return Result<std::unique_ptr<YoloClassifier>>::failure(
                ErrorCode::FILE_NOT_FOUND, "Model file not found: " + onnx_path);
        }
    }

    auto load_result = Inference::load(onnx_path);
    if (!load_result.ok()) {
        return Result<std::unique_ptr<YoloClassifier>>::failure(
            load_result.code, load_result.message);
    }

    std::unique_ptr<YoloClassifier> cls(new YoloClassifier());
    cls->impl_->engine = std::move(load_result.value);
    cls->impl_->names = class_names;

    // Determine input size from model metadata (expected: [1, 3, H, W])
    auto shape = cls->impl_->engine.input_shape();
    if (shape.size() == 4 && shape[2] > 0) {
        cls->impl_->input_size = shape[2];
    }

    return Result<std::unique_ptr<YoloClassifier>>::success(std::move(cls));
}

std::string YoloClassifier::name() const {
    return "YoloClassifier";
}

std::vector<std::string> YoloClassifier::class_names() const {
    return impl_->names;
}

// ---------------------------------------------------------------------------
// classify
// ---------------------------------------------------------------------------
Result<ClassifyResult> YoloClassifier::classify(const Image& image) {
    if (!image.buffer.data() || image.width <= 0 || image.height <= 0) {
        return Result<ClassifyResult>::failure(
            ErrorCode::INVALID_PARAMETER, "Invalid input image");
    }

    const int input_size = impl_->input_size;

    // Preprocess: resize to input_size x input_size, normalize, CHW
    auto tensor = yolo::preprocess_cls(image, input_size);

    // Build a temporary FLOAT32 image with the preprocessed data
    // The Inference::run() expects an Image; we need to pass the pre-built
    // tensor data. We'll create a FLOAT32 image and copy the CHW data.
    // However, Inference::run() already does HWC->CHW conversion.
    // Instead, we create an RGB8 resized image and let Inference handle it.
    Image resized = Image::create(input_size, input_size, PixelFormat::RGB8);
    uint8_t* dst = resized.buffer.data();

    // Convert CHW float [0,1] back to HWC uint8 for the Inference wrapper
    for (int y = 0; y < input_size; ++y) {
        for (int x = 0; x < input_size; ++x) {
            size_t base = static_cast<size_t>(y * input_size + x);
            float r = tensor[0 * input_size * input_size + base];
            float g = tensor[1 * input_size * input_size + base];
            float b = tensor[2 * input_size * input_size + base];
            dst[(y * input_size + x) * 3 + 0] = static_cast<uint8_t>(r * 255.0f);
            dst[(y * input_size + x) * 3 + 1] = static_cast<uint8_t>(g * 255.0f);
            dst[(y * input_size + x) * 3 + 2] = static_cast<uint8_t>(b * 255.0f);
        }
    }

    auto run_result = impl_->engine.run(resized);
    if (!run_result.ok()) {
        return Result<ClassifyResult>::failure(run_result.code, run_result.message);
    }

    auto& output = run_result.value;
    if (output.empty()) {
        return Result<ClassifyResult>::failure(
            ErrorCode::INTERNAL_ERROR, "Model produced empty output");
    }

    // Apply softmax to get probabilities
    yolo::softmax(output);

    // Find argmax
    int best_class = 0;
    float best_conf = output[0];
    for (size_t i = 1; i < output.size(); ++i) {
        if (output[i] > best_conf) {
            best_conf = output[i];
            best_class = static_cast<int>(i);
        }
    }

    ClassifyResult result;
    result.class_id = best_class;
    result.confidence = best_conf;

    if (best_class < static_cast<int>(impl_->names.size())) {
        result.class_name = impl_->names[static_cast<size_t>(best_class)];
    } else {
        result.class_name = "class_" + std::to_string(best_class);
    }

    // Build top-K (up to 5)
    std::vector<size_t> indices(output.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::partial_sort(indices.begin(),
                      indices.begin() + std::min(static_cast<size_t>(5), indices.size()),
                      indices.end(),
                      [&output](size_t a, size_t b) { return output[a] > output[b]; });

    int k = std::min(static_cast<int>(output.size()), 5);
    for (int i = 0; i < k; ++i) {
        size_t idx = indices[static_cast<size_t>(i)];
        std::string cname;
        if (static_cast<int>(idx) < static_cast<int>(impl_->names.size())) {
            cname = impl_->names[idx];
        } else {
            cname = "class_" + std::to_string(idx);
        }
        result.top_k.emplace_back(cname, output[idx]);
    }

    return Result<ClassifyResult>::success(std::move(result));
}

} // namespace vxl
