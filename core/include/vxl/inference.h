#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// InferenceParams -- configuration for ONNX Runtime session
// ---------------------------------------------------------------------------
struct VXL_EXPORT InferenceParams {
    std::string model_path;
    std::string device = "cpu";  // "cpu" or "cuda"
    int threads = 0;             // 0 = auto
};

// ---------------------------------------------------------------------------
// Inference -- ONNX Runtime wrapper (stub until onnxruntime is available)
// ---------------------------------------------------------------------------
class VXL_EXPORT Inference {
public:
    static Result<Inference> load(const std::string& onnx_path,
                                  const InferenceParams& params = {});

    // Run inference on image, return output as float tensor (flattened)
    Result<std::vector<float>> run(const Image& input) const;

    // Get model info
    std::string model_path() const;
    std::vector<int> input_shape() const;   // e.g., {1, 3, 224, 224}
    std::vector<int> output_shape() const;  // e.g., {1, 1000}

    ~Inference();
    Inference(Inference&&) noexcept;
    Inference& operator=(Inference&&) noexcept;

    Inference();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
