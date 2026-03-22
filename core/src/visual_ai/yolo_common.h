#pragma once
// Internal helpers shared by YOLO classifier/detector/segmenter.
// Not part of the public API.

#include "vxl/types.h"
#include "vxl/visual_ai.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace vxl {
namespace yolo {

// ---------------------------------------------------------------------------
// Letterbox info -- describes how an image was resized with padding
// ---------------------------------------------------------------------------
struct LetterboxInfo {
    float scale;       // scaling factor applied to the image
    float pad_x;       // horizontal padding (pixels in model input space)
    float pad_y;       // vertical padding (pixels in model input space)
    int orig_w;
    int orig_h;
    int input_size;
};

// ---------------------------------------------------------------------------
// channels_for_format (local helper)
// ---------------------------------------------------------------------------
inline int channels_for_format(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::GRAY8:   return 1;
        case PixelFormat::GRAY16:  return 1;
        case PixelFormat::RGB8:    return 3;
        case PixelFormat::BGR8:    return 3;
        case PixelFormat::FLOAT32: return 1;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// preprocess_yolo -- resize with letterbox padding, normalize to [0,1], CHW
// Returns a flattened float vector in NCHW format and the letterbox info.
// ---------------------------------------------------------------------------
inline std::pair<std::vector<float>, LetterboxInfo> preprocess_yolo(
    const Image& image, int input_size)
{
    const int orig_w = image.width;
    const int orig_h = image.height;
    const int c = channels_for_format(image.format);

    // Compute scale and padding for letterbox
    float scale = std::min(static_cast<float>(input_size) / orig_w,
                           static_cast<float>(input_size) / orig_h);
    int new_w = static_cast<int>(orig_w * scale);
    int new_h = static_cast<int>(orig_h * scale);

    float pad_x = (input_size - new_w) / 2.0f;
    float pad_y = (input_size - new_h) / 2.0f;

    LetterboxInfo info{scale, pad_x, pad_y, orig_w, orig_h, input_size};

    // Output tensor: [1, C_out, input_size, input_size] where C_out = 3
    const int out_c = 3;
    std::vector<float> tensor(static_cast<size_t>(out_c * input_size * input_size), 0.5f);
    // Initialize to 0.5 (grey padding value for YOLO letterbox)

    const uint8_t* src = image.buffer.data();
    const int stride = (image.stride > 0) ? image.stride : orig_w * c;
    const bool is_bgr = (image.format == PixelFormat::BGR8);
    const bool is_gray = (c == 1);

    // Bilinear interpolation for resized region
    for (int y = 0; y < new_h; ++y) {
        for (int x = 0; x < new_w; ++x) {
            // Map (x, y) in resized space back to source coordinates
            float src_x = x / scale;
            float src_y = y / scale;

            // Nearest-neighbor for simplicity (bilinear can be added later)
            int sx = std::min(static_cast<int>(src_x + 0.5f), orig_w - 1);
            int sy = std::min(static_cast<int>(src_y + 0.5f), orig_h - 1);

            int dst_x = x + static_cast<int>(pad_x);
            int dst_y = y + static_cast<int>(pad_y);

            if (dst_x >= input_size || dst_y >= input_size) continue;

            if (is_gray) {
                float val;
                if (image.format == PixelFormat::GRAY16) {
                    const uint16_t* row = reinterpret_cast<const uint16_t*>(src + sy * stride);
                    val = static_cast<float>(row[sx]) / 65535.0f;
                } else if (image.format == PixelFormat::FLOAT32) {
                    const float* row = reinterpret_cast<const float*>(src + sy * stride);
                    val = row[sx];
                } else {
                    val = static_cast<float>(src[sy * stride + sx]) / 255.0f;
                }
                // Replicate to 3 channels
                for (int ch = 0; ch < 3; ++ch) {
                    size_t idx = static_cast<size_t>(ch * input_size * input_size +
                                                     dst_y * input_size + dst_x);
                    tensor[idx] = val;
                }
            } else {
                const uint8_t* pixel = src + sy * stride + sx * c;
                float r, g, b;
                if (is_bgr) {
                    b = static_cast<float>(pixel[0]) / 255.0f;
                    g = static_cast<float>(pixel[1]) / 255.0f;
                    r = static_cast<float>(pixel[2]) / 255.0f;
                } else {
                    r = static_cast<float>(pixel[0]) / 255.0f;
                    g = static_cast<float>(pixel[1]) / 255.0f;
                    b = static_cast<float>(pixel[2]) / 255.0f;
                }
                size_t base = static_cast<size_t>(dst_y * input_size + dst_x);
                tensor[0 * input_size * input_size + base] = r;
                tensor[1 * input_size * input_size + base] = g;
                tensor[2 * input_size * input_size + base] = b;
            }
        }
    }

    return {std::move(tensor), info};
}

// ---------------------------------------------------------------------------
// preprocess_cls -- resize to (input_size x input_size) without letterbox,
// normalize to [0,1], CHW.  Classification models typically use 224x224.
// ---------------------------------------------------------------------------
inline std::vector<float> preprocess_cls(const Image& image, int input_size) {
    const int orig_w = image.width;
    const int orig_h = image.height;
    const int c = channels_for_format(image.format);
    const int out_c = 3;

    std::vector<float> tensor(static_cast<size_t>(out_c * input_size * input_size), 0.0f);

    const uint8_t* src = image.buffer.data();
    const int stride = (image.stride > 0) ? image.stride : orig_w * c;
    const bool is_bgr = (image.format == PixelFormat::BGR8);
    const bool is_gray = (c == 1);

    for (int y = 0; y < input_size; ++y) {
        for (int x = 0; x < input_size; ++x) {
            float src_x = static_cast<float>(x) * orig_w / input_size;
            float src_y = static_cast<float>(y) * orig_h / input_size;

            int sx = std::min(static_cast<int>(src_x), orig_w - 1);
            int sy = std::min(static_cast<int>(src_y), orig_h - 1);

            if (is_gray) {
                float val;
                if (image.format == PixelFormat::GRAY16) {
                    const uint16_t* row = reinterpret_cast<const uint16_t*>(src + sy * stride);
                    val = static_cast<float>(row[sx]) / 65535.0f;
                } else if (image.format == PixelFormat::FLOAT32) {
                    const float* row = reinterpret_cast<const float*>(src + sy * stride);
                    val = row[sx];
                } else {
                    val = static_cast<float>(src[sy * stride + sx]) / 255.0f;
                }
                for (int ch = 0; ch < 3; ++ch) {
                    tensor[static_cast<size_t>(ch * input_size * input_size +
                                               y * input_size + x)] = val;
                }
            } else {
                const uint8_t* pixel = src + sy * stride + sx * c;
                float r, g, b;
                if (is_bgr) {
                    b = static_cast<float>(pixel[0]) / 255.0f;
                    g = static_cast<float>(pixel[1]) / 255.0f;
                    r = static_cast<float>(pixel[2]) / 255.0f;
                } else {
                    r = static_cast<float>(pixel[0]) / 255.0f;
                    g = static_cast<float>(pixel[1]) / 255.0f;
                    b = static_cast<float>(pixel[2]) / 255.0f;
                }
                size_t base = static_cast<size_t>(y * input_size + x);
                tensor[0 * input_size * input_size + base] = r;
                tensor[1 * input_size * input_size + base] = g;
                tensor[2 * input_size * input_size + base] = b;
            }
        }
    }

    return tensor;
}

// ---------------------------------------------------------------------------
// xywh_to_xyxy -- convert center-x, center-y, width, height to x1,y1,x2,y2
// ---------------------------------------------------------------------------
inline void xywh_to_xyxy(float cx, float cy, float w, float h,
                          float& x1, float& y1, float& x2, float& y2) {
    x1 = cx - w * 0.5f;
    y1 = cy - h * 0.5f;
    x2 = cx + w * 0.5f;
    y2 = cy + h * 0.5f;
}

// ---------------------------------------------------------------------------
// letterbox_to_original -- scale bbox from model input coords to original
// ---------------------------------------------------------------------------
inline ROI letterbox_to_original(float x1, float y1, float x2, float y2,
                                  const LetterboxInfo& info) {
    // Remove padding offset, then un-scale
    x1 = (x1 - info.pad_x) / info.scale;
    y1 = (y1 - info.pad_y) / info.scale;
    x2 = (x2 - info.pad_x) / info.scale;
    y2 = (y2 - info.pad_y) / info.scale;

    // Clamp to original image bounds
    x1 = std::max(0.0f, std::min(x1, static_cast<float>(info.orig_w)));
    y1 = std::max(0.0f, std::min(y1, static_cast<float>(info.orig_h)));
    x2 = std::max(0.0f, std::min(x2, static_cast<float>(info.orig_w)));
    y2 = std::max(0.0f, std::min(y2, static_cast<float>(info.orig_h)));

    ROI roi;
    roi.x = static_cast<int>(x1);
    roi.y = static_cast<int>(y1);
    roi.w = static_cast<int>(x2 - x1);
    roi.h = static_cast<int>(y2 - y1);
    return roi;
}

// ---------------------------------------------------------------------------
// compute_iou -- intersection-over-union between two boxes (x1,y1,x2,y2)
// ---------------------------------------------------------------------------
inline float compute_iou(float ax1, float ay1, float ax2, float ay2,
                          float bx1, float by1, float bx2, float by2) {
    float ix1 = std::max(ax1, bx1);
    float iy1 = std::max(ay1, by1);
    float ix2 = std::min(ax2, bx2);
    float iy2 = std::min(ay2, by2);

    float iw = std::max(0.0f, ix2 - ix1);
    float ih = std::max(0.0f, iy2 - iy1);
    float intersection = iw * ih;

    float area_a = (ax2 - ax1) * (ay2 - ay1);
    float area_b = (bx2 - bx1) * (by2 - by1);
    float union_area = area_a + area_b - intersection;

    return (union_area > 0.0f) ? intersection / union_area : 0.0f;
}

// ---------------------------------------------------------------------------
// DetCandidate -- internal detection candidate for NMS
// ---------------------------------------------------------------------------
struct DetCandidate {
    float x1, y1, x2, y2;
    float confidence;
    int class_id;
    std::vector<float> mask_coeffs;  // for segmentation
};

// ---------------------------------------------------------------------------
// nms -- non-maximum suppression
// ---------------------------------------------------------------------------
inline std::vector<DetCandidate> nms(std::vector<DetCandidate>& candidates,
                                      float iou_threshold) {
    // Sort by confidence descending
    std::sort(candidates.begin(), candidates.end(),
              [](const DetCandidate& a, const DetCandidate& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> suppressed(candidates.size(), false);
    std::vector<DetCandidate> result;

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i]) continue;
        result.push_back(candidates[i]);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (suppressed[j]) continue;
            float iou = compute_iou(candidates[i].x1, candidates[i].y1,
                                     candidates[i].x2, candidates[i].y2,
                                     candidates[j].x1, candidates[j].y1,
                                     candidates[j].x2, candidates[j].y2);
            if (iou > iou_threshold) {
                suppressed[j] = true;
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// softmax -- in-place softmax over a float vector
// ---------------------------------------------------------------------------
inline void softmax(std::vector<float>& v) {
    if (v.empty()) return;
    float max_val = *std::max_element(v.begin(), v.end());
    float sum = 0.0f;
    for (auto& x : v) {
        x = std::exp(x - max_val);
        sum += x;
    }
    if (sum > 0.0f) {
        for (auto& x : v) {
            x /= sum;
        }
    }
}

// ---------------------------------------------------------------------------
// sigmoid
// ---------------------------------------------------------------------------
inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

} // namespace yolo
} // namespace vxl
