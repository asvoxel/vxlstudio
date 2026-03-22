// Compute backend manager -- owns the engine instances and dispatches
// set/get/available/engine queries.

#include "vxl/compute.h"

#include "cpu_engine.h"
#include "cuda_engine.h"
#include "metal_engine.h"

#include <mutex>

namespace vxl {
namespace {

// All engine singletons, protected by a mutex for thread safety.
struct EngineRegistry {
    std::mutex mtx;
    ComputeBackend current = ComputeBackend::CPU;

    CpuEngine cpu;
#ifdef VXL_HAS_CUDA
    CudaEngine cuda;
#endif
#ifdef VXL_HAS_METAL
    MetalEngine metal;
#endif

    IComputeEngine* engine_for(ComputeBackend b) {
        switch (b) {
        case ComputeBackend::CPU:    return &cpu;
#ifdef VXL_HAS_CUDA
        case ComputeBackend::CUDA:   return &cuda;
#endif
#ifdef VXL_HAS_METAL
        case ComputeBackend::METAL:  return &metal;
#endif
        default:                     return nullptr;
        }
    }
};

EngineRegistry& registry() {
    static EngineRegistry r;
    return r;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result<void> set_compute_backend(ComputeBackend backend) {
    auto& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mtx);

    IComputeEngine* eng = reg.engine_for(backend);
    if (!eng || !eng->is_available()) {
        return Result<void>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Compute backend '" + (eng ? eng->name() : "unknown") +
            "' is not available on this system");
    }

    reg.current = backend;
    return Result<void>::success();
}

ComputeBackend get_compute_backend() {
    auto& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mtx);
    return reg.current;
}

std::vector<ComputeBackend> available_backends() {
    auto& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mtx);

    std::vector<ComputeBackend> out;

    auto try_add = [&](ComputeBackend b) {
        IComputeEngine* e = reg.engine_for(b);
        if (e && e->is_available()) out.push_back(b);
    };

    try_add(ComputeBackend::CPU);
    try_add(ComputeBackend::CUDA);
    try_add(ComputeBackend::METAL);
    try_add(ComputeBackend::OPENCL);

    return out;
}

IComputeEngine& compute_engine() {
    auto& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mtx);
    IComputeEngine* eng = reg.engine_for(reg.current);
    // CPU is always available, so this should never be null after init.
    return *eng;
}

} // namespace vxl
