#include "vxl/visual_ai.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numeric>

namespace vxl {

// ---------------------------------------------------------------------------
// AnomalibDetector::Impl
// ---------------------------------------------------------------------------
struct AnomalibDetector::Impl {
    Inference engine;
    std::string model_name;
    int input_h = 256;
    int input_w = 256;
};

AnomalibDetector::AnomalibDetector() : impl_(std::make_unique<Impl>()) {}
AnomalibDetector::~AnomalibDetector() = default;

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------
Result<std::unique_ptr<AnomalibDetector>> AnomalibDetector::create(
    const std::string& onnx_path)
{
    if (onnx_path.empty()) {
        return Result<std::unique_ptr<AnomalibDetector>>::failure(
            ErrorCode::INVALID_PARAMETER, "Model path is empty");
    }

    // Check file existence
    {
        std::ifstream f(onnx_path);
        if (!f.good()) {
            return Result<std::unique_ptr<AnomalibDetector>>::failure(
                ErrorCode::FILE_NOT_FOUND, "Model file not found: " + onnx_path);
        }
    }

    // Load ONNX model via Inference wrapper
    auto load_result = Inference::load(onnx_path);
    if (!load_result.ok()) {
        return Result<std::unique_ptr<AnomalibDetector>>::failure(
            load_result.code, load_result.message);
    }

    std::unique_ptr<AnomalibDetector> detector(new AnomalibDetector());
    detector->impl_->engine = std::move(load_result.value);
    detector->impl_->model_name = onnx_path;

    // Determine input size from model metadata (expected: [1, 3, H, W])
    auto shape = detector->impl_->engine.input_shape();
    if (shape.size() == 4) {
        detector->impl_->input_h = (shape[2] > 0) ? shape[2] : 256;
        detector->impl_->input_w = (shape[3] > 0) ? shape[3] : 256;
    }

    return Result<std::unique_ptr<AnomalibDetector>>::success(std::move(detector));
}

std::string AnomalibDetector::name() const {
    return "AnomalibDetector";
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------
Result<AnomalyResult> AnomalibDetector::detect(const Image& image, float threshold) {
    if (!image.buffer.data() || image.width <= 0 || image.height <= 0) {
        return Result<AnomalyResult>::failure(
            ErrorCode::INVALID_PARAMETER, "Invalid input image");
    }

    const int target_h = impl_->input_h;
    const int target_w = impl_->input_w;

    // Build a resized image for inference.
    // The Inference::run() handles image-to-tensor conversion internally,
    // but we need to resize to the expected input dimensions first.
    // We create a new Image with the target size.
    Image resized = Image::create(target_w, target_h, PixelFormat::RGB8);

    // Simple nearest-neighbor resize
    const int orig_w = image.width;
    const int orig_h = image.height;
    const int c_in = (image.format == PixelFormat::GRAY8 || image.format == PixelFormat::GRAY16 ||
                      image.format == PixelFormat::FLOAT32) ? 1 : 3;
    const int c_out = 3;
    const uint8_t* src = image.buffer.data();
    uint8_t* dst = resized.buffer.data();
    const int src_stride = (image.stride > 0) ? image.stride : orig_w * c_in;
    const int dst_stride = target_w * c_out;
    const bool is_bgr = (image.format == PixelFormat::BGR8);

    for (int y = 0; y < target_h; ++y) {
        int sy = y * orig_h / target_h;
        sy = std::min(sy, orig_h - 1);
        for (int x = 0; x < target_w; ++x) {
            int sx = x * orig_w / target_w;
            sx = std::min(sx, orig_w - 1);

            uint8_t r, g, b;
            if (c_in == 1) {
                uint8_t v;
                if (image.format == PixelFormat::GRAY16) {
                    const uint16_t* row = reinterpret_cast<const uint16_t*>(src + sy * src_stride);
                    v = static_cast<uint8_t>(row[sx] >> 8);
                } else if (image.format == PixelFormat::FLOAT32) {
                    const float* row = reinterpret_cast<const float*>(src + sy * src_stride);
                    v = static_cast<uint8_t>(std::min(1.0f, std::max(0.0f, row[sx])) * 255.0f);
                } else {
                    v = src[sy * src_stride + sx];
                }
                r = g = b = v;
            } else {
                const uint8_t* pixel = src + sy * src_stride + sx * c_in;
                if (is_bgr) {
                    b = pixel[0]; g = pixel[1]; r = pixel[2];
                } else {
                    r = pixel[0]; g = pixel[1]; b = pixel[2];
                }
            }
            dst[y * dst_stride + x * 3 + 0] = r;
            dst[y * dst_stride + x * 3 + 1] = g;
            dst[y * dst_stride + x * 3 + 2] = b;
        }
    }

    // Run inference
    auto run_result = impl_->engine.run(resized);
    if (!run_result.ok()) {
        return Result<AnomalyResult>::failure(run_result.code, run_result.message);
    }

    const auto& output = run_result.value;
    if (output.empty()) {
        return Result<AnomalyResult>::failure(
            ErrorCode::INTERNAL_ERROR, "Model produced empty output");
    }

    AnomalyResult result;

    // Check output shape from model metadata
    auto out_shape = impl_->engine.output_shape();

    // Case 1: scalar output (anomaly score only) -- output size == 1
    // Case 2: spatial map [1, 1, H, W] -- output size > 1
    if (output.size() == 1) {
        // Scalar anomaly score
        result.score = output[0];
        // No heatmap
    } else {
        // Spatial anomaly map: max value is the anomaly score
        result.score = *std::max_element(output.begin(), output.end());

        // Build heatmap: normalize output to [0, 255] and write as GRAY8
        int map_h = target_h;
        int map_w = target_w;

        // Try to derive spatial dims from output shape
        if (out_shape.size() == 4 && out_shape[2] > 0 && out_shape[3] > 0) {
            map_h = out_shape[2];
            map_w = out_shape[3];
        } else {
            // Assume square output
            int total = static_cast<int>(output.size());
            int side = static_cast<int>(std::sqrt(static_cast<float>(total)));
            if (side * side == total) {
                map_h = map_w = side;
            }
        }

        if (static_cast<int>(output.size()) >= map_h * map_w) {
            result.heatmap = Image::create(map_w, map_h, PixelFormat::GRAY8);
            uint8_t* hm = result.heatmap.buffer.data();

            // Find min/max for normalization
            float min_val = *std::min_element(output.begin(),
                                               output.begin() + map_h * map_w);
            float max_val = *std::max_element(output.begin(),
                                               output.begin() + map_h * map_w);
            float range = max_val - min_val;
            if (range < 1e-8f) range = 1.0f;

            for (int i = 0; i < map_h * map_w; ++i) {
                float normalized = (output[static_cast<size_t>(i)] - min_val) / range;
                hm[i] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, normalized * 255.0f)));
            }
        }
    }

    result.is_anomalous = (result.score > threshold);

    return Result<AnomalyResult>::success(std::move(result));
}

} // namespace vxl
