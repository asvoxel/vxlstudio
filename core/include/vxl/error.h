#pragma once

#include <functional>
#include <string>
#include <utility>

#include "vxl/export.h"

namespace vxl {

enum class ErrorCode : int {
    OK = 0,
    DEVICE_NOT_FOUND,
    DEVICE_OPEN_FAILED,
    DEVICE_TIMEOUT,
    DEVICE_DISCONNECTED,
    CALIB_INSUFFICIENT_DATA,
    CALIB_CONVERGENCE_FAILED,
    RECONSTRUCT_LOW_MODULATION,
    RECONSTRUCT_PHASE_UNWRAP_FAILED,
    INSPECT_NO_REFERENCE,
    INSPECT_ROI_OUT_OF_BOUNDS,
    MODEL_LOAD_FAILED,
    MODEL_INPUT_MISMATCH,
    IO_CONNECTION_FAILED,
    IO_WRITE_FAILED,
    IO_READ_FAILED,
    IO_NOT_SUPPORTED,
    INVALID_PARAMETER,
    FILE_NOT_FOUND,
    OUT_OF_MEMORY,
    INTERNAL_ERROR
};

VXL_EXPORT const char* error_code_to_string(ErrorCode code);

template <typename T>
struct Result {
    ErrorCode code = ErrorCode::OK;
    std::string message;
    T value{};

    bool ok() const { return code == ErrorCode::OK; }
    explicit operator bool() const { return ok(); }

    static Result success(T val) {
        return Result{ErrorCode::OK, {}, std::move(val)};
    }

    static Result failure(ErrorCode c, std::string msg = {}) {
        if (msg.empty()) {
            msg = error_code_to_string(c);
        }
        return Result{c, std::move(msg), T{}};
    }
};

template <>
struct Result<void> {
    ErrorCode code = ErrorCode::OK;
    std::string message;

    bool ok() const { return code == ErrorCode::OK; }
    explicit operator bool() const { return ok(); }

    static Result success() {
        return Result{ErrorCode::OK, {}};
    }

    static Result failure(ErrorCode c, std::string msg = {}) {
        if (msg.empty()) {
            msg = error_code_to_string(c);
        }
        return Result{c, std::move(msg)};
    }
};

using ErrorCallback = std::function<void(ErrorCode, const std::string&)>;

VXL_EXPORT void set_error_callback(ErrorCallback callback);

} // namespace vxl
