#include "vxl/inspector_2d.h"

#include <algorithm>

namespace vxl {

Result<float> Inspector2D::anomaly_detect(
    const Image& image, const Inference* model,
    float /*threshold*/) {
    if (!model) {
        return Result<float>::failure(ErrorCode::MODEL_LOAD_FAILED,
            "Anomaly detection requires an inference model");
    }

    auto output = model->run(image);
    if (!output.ok()) {
        return Result<float>::failure(output.code, output.message);
    }

    if (output.value.empty()) {
        return Result<float>::failure(ErrorCode::INTERNAL_ERROR,
            "Model produced empty output");
    }

    // Anomaly score = max of output tensor (typical for Anomalib models)
    float score = *std::max_element(output.value.begin(), output.value.end());
    return Result<float>::success(score);
}

} // namespace vxl
