#include "vxl/error.h"

#include <mutex>

namespace vxl {

static ErrorCallback g_error_callback;
static std::mutex g_error_callback_mutex;

const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK:                             return "OK";
        case ErrorCode::DEVICE_NOT_FOUND:               return "Device not found";
        case ErrorCode::DEVICE_OPEN_FAILED:             return "Device open failed";
        case ErrorCode::DEVICE_TIMEOUT:                 return "Device timeout";
        case ErrorCode::DEVICE_DISCONNECTED:            return "Device disconnected";
        case ErrorCode::CALIB_INSUFFICIENT_DATA:        return "Calibration: insufficient data";
        case ErrorCode::CALIB_CONVERGENCE_FAILED:       return "Calibration: convergence failed";
        case ErrorCode::RECONSTRUCT_LOW_MODULATION:     return "Reconstruction: low modulation";
        case ErrorCode::RECONSTRUCT_PHASE_UNWRAP_FAILED:return "Reconstruction: phase unwrap failed";
        case ErrorCode::INSPECT_NO_REFERENCE:           return "Inspection: no reference";
        case ErrorCode::INSPECT_ROI_OUT_OF_BOUNDS:      return "Inspection: ROI out of bounds";
        case ErrorCode::MODEL_LOAD_FAILED:              return "Model load failed";
        case ErrorCode::MODEL_INPUT_MISMATCH:           return "Model input mismatch";
        case ErrorCode::IO_CONNECTION_FAILED:           return "IO connection failed";
        case ErrorCode::IO_WRITE_FAILED:                return "IO write failed";
        case ErrorCode::IO_READ_FAILED:                 return "IO read failed";
        case ErrorCode::IO_NOT_SUPPORTED:               return "IO operation not supported";
        case ErrorCode::INVALID_PARAMETER:              return "Invalid parameter";
        case ErrorCode::FILE_NOT_FOUND:                 return "File not found";
        case ErrorCode::OUT_OF_MEMORY:                  return "Out of memory";
        case ErrorCode::INTERNAL_ERROR:                 return "Internal error";
        default:                                        return "Unknown error";
    }
}

void set_error_callback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(g_error_callback_mutex);
    g_error_callback = std::move(callback);
}

} // namespace vxl
