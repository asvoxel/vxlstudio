#include "vxl/inference.h"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <vector>

#ifndef VXL_NO_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace vxl {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
#ifndef VXL_NO_ONNXRUNTIME

struct Inference::Impl {
    Ort::Env env{nullptr};
    std::unique_ptr<Ort::Session> session;
    std::string path;

    std::vector<int> input_shape;
    std::vector<int> output_shape;

    std::vector<std::string> input_names;
    std::vector<std::string> output_names;

    // Keep raw C-string pointers for Run() calls (point into the vectors above)
    std::vector<const char*> input_name_ptrs;
    std::vector<const char*> output_name_ptrs;
};

#else  // VXL_NO_ONNXRUNTIME -- stub Impl

struct Inference::Impl {
    std::string path;
    bool        loaded = false;
};

#endif // VXL_NO_ONNXRUNTIME

// ---------------------------------------------------------------------------
// Construction / destruction / move
// ---------------------------------------------------------------------------
Inference::Inference() : impl_(std::make_unique<Impl>()) {}
Inference::~Inference() = default;

Inference::Inference(Inference&&) noexcept = default;
Inference& Inference::operator=(Inference&&) noexcept = default;

// ---------------------------------------------------------------------------
// Helper: number of channels for a PixelFormat
// ---------------------------------------------------------------------------
static int channels_for_format(PixelFormat fmt) {
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
// Helper: bytes per channel element
// ---------------------------------------------------------------------------
static int bytes_per_element(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::GRAY8:   return 1;
        case PixelFormat::GRAY16:  return 2;
        case PixelFormat::RGB8:    return 1;
        case PixelFormat::BGR8:    return 1;
        case PixelFormat::FLOAT32: return 4;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
Result<Inference> Inference::load(const std::string& onnx_path,
                                  const InferenceParams& params) {
    if (onnx_path.empty()) {
        return Result<Inference>::failure(ErrorCode::INVALID_PARAMETER,
                                         "Model path is empty");
    }

    // Check if the file exists.
    {
        std::ifstream f(onnx_path);
        if (!f.good()) {
            return Result<Inference>::failure(ErrorCode::FILE_NOT_FOUND,
                                             "Model file not found: " + onnx_path);
        }
    }

#ifndef VXL_NO_ONNXRUNTIME
    try {
        Inference inf;
        auto& impl = *inf.impl_;
        impl.path = onnx_path;

        // 1. Create Ort::Env
        impl.env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "vxl_inference");

        // 2. Session options
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (params.threads > 0) {
            opts.SetIntraOpNumThreads(params.threads);
        }

        // 3. Create session
        impl.session = std::make_unique<Ort::Session>(impl.env, onnx_path.c_str(), opts);

        Ort::AllocatorWithDefaultOptions allocator;

        // 4. Read input metadata
        const size_t num_inputs = impl.session->GetInputCount();
        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = impl.session->GetInputNameAllocated(i, allocator);
            impl.input_names.emplace_back(name.get());

            auto type_info = impl.session->GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            if (i == 0) {
                impl.input_shape.clear();
                for (auto d : shape) {
                    impl.input_shape.push_back(static_cast<int>(d));
                }
            }
        }

        // 5. Read output metadata
        const size_t num_outputs = impl.session->GetOutputCount();
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = impl.session->GetOutputNameAllocated(i, allocator);
            impl.output_names.emplace_back(name.get());

            auto type_info = impl.session->GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            if (i == 0) {
                impl.output_shape.clear();
                for (auto d : shape) {
                    impl.output_shape.push_back(static_cast<int>(d));
                }
            }
        }

        // Build raw C-string pointers for Run()
        impl.input_name_ptrs.clear();
        for (const auto& n : impl.input_names) {
            impl.input_name_ptrs.push_back(n.c_str());
        }
        impl.output_name_ptrs.clear();
        for (const auto& n : impl.output_names) {
            impl.output_name_ptrs.push_back(n.c_str());
        }

        return Result<Inference>::success(std::move(inf));

    } catch (const Ort::Exception& e) {
        return Result<Inference>::failure(ErrorCode::MODEL_LOAD_FAILED,
                                         std::string("ONNX Runtime error: ") + e.what());
    } catch (const std::exception& e) {
        return Result<Inference>::failure(ErrorCode::MODEL_LOAD_FAILED,
                                         std::string("Error loading model: ") + e.what());
    }

#else  // VXL_NO_ONNXRUNTIME

    Inference inf;
    inf.impl_->path   = onnx_path;
    inf.impl_->loaded = false;

    return Result<Inference>::success(std::move(inf));
#endif
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
Result<std::vector<float>> Inference::run(const Image& input) const {
#ifndef VXL_NO_ONNXRUNTIME
    if (!impl_ || !impl_->session) {
        return Result<std::vector<float>>::failure(
            ErrorCode::MODEL_LOAD_FAILED, "No model loaded");
    }

    try {
        const auto& impl = *impl_;

        // Determine image dimensions
        const int h = input.height;
        const int w = input.width;
        const int c = channels_for_format(input.format);
        const int bpe = bytes_per_element(input.format);

        // Build input shape: use model shape if available, otherwise derive from image
        std::vector<int64_t> tensor_shape;
        if (!impl.input_shape.empty()) {
            tensor_shape.reserve(impl.input_shape.size());
            for (auto d : impl.input_shape) {
                tensor_shape.push_back(static_cast<int64_t>(d));
            }
            // Replace dynamic dims (-1) with actual image dims (NCHW assumed)
            if (tensor_shape.size() == 4) {
                if (tensor_shape[0] <= 0) tensor_shape[0] = 1;
                if (tensor_shape[1] <= 0) tensor_shape[1] = c;
                if (tensor_shape[2] <= 0) tensor_shape[2] = h;
                if (tensor_shape[3] <= 0) tensor_shape[3] = w;
            }
        } else {
            tensor_shape = {1, static_cast<int64_t>(c),
                            static_cast<int64_t>(h),
                            static_cast<int64_t>(w)};
        }

        const size_t total_elements = static_cast<size_t>(std::accumulate(
            tensor_shape.begin(), tensor_shape.end(),
            int64_t{1}, std::multiplies<int64_t>()));

        // Convert image to float tensor (CHW, normalized 0-1)
        std::vector<float> tensor_data(total_elements);

        const uint8_t* src = input.buffer.data();
        if (!src) {
            return Result<std::vector<float>>::failure(
                ErrorCode::INVALID_PARAMETER, "Image buffer is null");
        }

        const int stride = (input.stride > 0) ? input.stride : w * c * bpe;

        if (input.format == PixelFormat::FLOAT32) {
            // Already float -- just copy (HWC -> CHW if c > 1)
            for (int ch = 0; ch < c; ++ch) {
                for (int y = 0; y < h; ++y) {
                    const float* row = reinterpret_cast<const float*>(src + y * stride);
                    for (int x = 0; x < w; ++x) {
                        size_t idx = static_cast<size_t>(ch * h * w + y * w + x);
                        if (idx < total_elements) {
                            tensor_data[idx] = row[x * c + ch];
                        }
                    }
                }
            }
        } else if (input.format == PixelFormat::GRAY16) {
            for (int y = 0; y < h; ++y) {
                const uint16_t* row = reinterpret_cast<const uint16_t*>(src + y * stride);
                for (int x = 0; x < w; ++x) {
                    size_t idx = static_cast<size_t>(y * w + x);
                    if (idx < total_elements) {
                        tensor_data[idx] = static_cast<float>(row[x]) / 65535.0f;
                    }
                }
            }
        } else {
            // uint8 formats: GRAY8, RGB8, BGR8
            bool swap_rb = (input.format == PixelFormat::BGR8);
            for (int ch = 0; ch < c; ++ch) {
                int src_ch = ch;
                if (swap_rb && c == 3) {
                    // BGR -> RGB: swap channel 0 and 2
                    if (ch == 0) src_ch = 2;
                    else if (ch == 2) src_ch = 0;
                }
                for (int y = 0; y < h; ++y) {
                    const uint8_t* row = src + y * stride;
                    for (int x = 0; x < w; ++x) {
                        size_t idx = static_cast<size_t>(ch * h * w + y * w + x);
                        if (idx < total_elements) {
                            tensor_data[idx] =
                                static_cast<float>(row[x * c + src_ch]) / 255.0f;
                        }
                    }
                }
            }
        }

        // Create Ort::Value
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);

        auto input_tensor = Ort::Value::CreateTensor<float>(
            mem_info, tensor_data.data(), total_elements,
            tensor_shape.data(), tensor_shape.size());

        // Run session
        auto output_tensors = impl.session->Run(
            Ort::RunOptions{nullptr},
            impl.input_name_ptrs.data(), &input_tensor, 1,
            impl.output_name_ptrs.data(), impl.output_name_ptrs.size());

        if (output_tensors.empty() || !output_tensors[0].IsTensor()) {
            return Result<std::vector<float>>::failure(
                ErrorCode::INTERNAL_ERROR, "Model produced no tensor output");
        }

        // Extract output
        auto& out_tensor = output_tensors[0];
        auto out_info = out_tensor.GetTensorTypeAndShapeInfo();
        size_t out_count = out_info.GetElementCount();

        const float* out_data = out_tensor.GetTensorData<float>();
        std::vector<float> result(out_data, out_data + out_count);

        return Result<std::vector<float>>::success(std::move(result));

    } catch (const Ort::Exception& e) {
        return Result<std::vector<float>>::failure(
            ErrorCode::INTERNAL_ERROR,
            std::string("ONNX Runtime inference error: ") + e.what());
    } catch (const std::exception& e) {
        return Result<std::vector<float>>::failure(
            ErrorCode::INTERNAL_ERROR,
            std::string("Inference error: ") + e.what());
    }

#else  // VXL_NO_ONNXRUNTIME
    (void)input;
    return Result<std::vector<float>>::failure(
        ErrorCode::MODEL_LOAD_FAILED,
        "ONNX Runtime not available (stub implementation)");
#endif
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
std::string Inference::model_path() const {
    return impl_ ? impl_->path : std::string{};
}

std::vector<int> Inference::input_shape() const {
#ifndef VXL_NO_ONNXRUNTIME
    return impl_ ? impl_->input_shape : std::vector<int>{};
#else
    return {};
#endif
}

std::vector<int> Inference::output_shape() const {
#ifndef VXL_NO_ONNXRUNTIME
    return impl_ ? impl_->output_shape : std::vector<int>{};
#else
    return {};
#endif
}

} // namespace vxl
