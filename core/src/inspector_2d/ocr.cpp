#include "vxl/inspector_2d.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vxl {

// Basic printable ASCII charset used for CTC/argmax decoding.
// Index 0 is reserved for the CTC blank token; indices 1-95 map to
// characters ' ' (0x20) through '~' (0x7E).
static char index_to_char(int idx) {
    if (idx <= 0) return '\0';            // blank / padding
    if (idx >= 1 && idx <= 95) return static_cast<char>(0x1F + idx); // ' ' .. '~'
    return '?';
}

Result<OcrResult> Inspector2D::ocr(
    const Image& image, const ROI& roi,
    const Inference* model) {
    if (!model) {
        return Result<OcrResult>::failure(
            ErrorCode::MODEL_LOAD_FAILED,
            "OCR requires an inference model");
    }

    // Run inference on the full image (caller should crop to ROI beforehand
    // if the model expects a cropped region; for simplicity we pass as-is).
    auto output = model->run(image);
    if (!output.ok()) {
        return Result<OcrResult>::failure(output.code, output.message);
    }

    if (output.value.empty()) {
        return Result<OcrResult>::failure(ErrorCode::INTERNAL_ERROR,
            "Model produced empty output");
    }

    // Decode output assuming shape [1, T, num_classes] (CTC-style):
    //   - T = sequence length (time steps)
    //   - num_classes includes blank at index 0
    //
    // If the model output shape is available, use it to determine T and
    // num_classes; otherwise fall back to treating the entire output as a
    // flat probability vector over one time step.
    auto model_out_shape = model->output_shape();

    int seq_len     = 1;
    int num_classes  = static_cast<int>(output.value.size());

    if (model_out_shape.size() == 3) {
        // [batch, T, C]
        seq_len     = model_out_shape[1];
        num_classes = model_out_shape[2];
    } else if (model_out_shape.size() == 2) {
        // [T, C]
        seq_len     = model_out_shape[0];
        num_classes = model_out_shape[1];
    }

    // Safety: make sure dimensions are consistent with actual data
    if (seq_len <= 0 || num_classes <= 0 ||
        static_cast<size_t>(seq_len) * static_cast<size_t>(num_classes) > output.value.size()) {
        seq_len     = 1;
        num_classes = static_cast<int>(output.value.size());
    }

    // Greedy / argmax decode with CTC blank collapse
    std::string text;
    float total_conf = 0.0f;
    int   char_count = 0;
    int   prev_idx   = -1;

    for (int t = 0; t < seq_len; ++t) {
        const float* logits = output.value.data() + t * num_classes;

        // argmax
        int best_idx = 0;
        float best_val = logits[0];
        for (int c = 1; c < num_classes; ++c) {
            if (logits[c] > best_val) {
                best_val = logits[c];
                best_idx = c;
            }
        }

        // CTC: skip blank (0) and repeated indices
        if (best_idx != 0 && best_idx != prev_idx) {
            char ch = index_to_char(best_idx);
            if (ch != '\0') {
                text += ch;
                total_conf += best_val;
                ++char_count;
            }
        }
        prev_idx = best_idx;
    }

    float confidence = (char_count > 0)
                       ? (total_conf / static_cast<float>(char_count))
                       : 0.0f;

    OcrResult result;
    result.text       = std::move(text);
    result.confidence = confidence;
    result.region     = roi;

    return Result<OcrResult>::success(std::move(result));
}

} // namespace vxl
