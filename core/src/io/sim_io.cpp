#include "sim_io.h"

namespace vxl {

std::string SimIO::device_id() const { return "SIM-IO"; }

Result<void> SimIO::open() {
    open_ = true;
    return Result<void>::success();
}

void SimIO::close() {
    open_ = false;
}

bool SimIO::is_open() const { return open_; }

Result<void> SimIO::set_output(const std::string& pin, bool value) {
    if (!open_) {
        return Result<void>::failure(ErrorCode::DEVICE_OPEN_FAILED,
                                    "IO device is not open");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pins_[pin] = value;
    return Result<void>::success();
}

Result<bool> SimIO::get_input(const std::string& pin) {
    if (!open_) {
        return Result<bool>::failure(ErrorCode::DEVICE_OPEN_FAILED,
                                    "IO device is not open");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pins_.find(pin);
    if (it != pins_.end()) {
        return Result<bool>::success(it->second);
    }
    return Result<bool>::success(false);  // default to false for unset pins
}

Result<void> SimIO::write_register(int address, uint16_t value) {
    if (!open_) {
        return Result<void>::failure(ErrorCode::DEVICE_OPEN_FAILED,
                                    "IO device is not open");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    registers_[address] = value;
    return Result<void>::success();
}

Result<uint16_t> SimIO::read_register(int address) {
    if (!open_) {
        return Result<uint16_t>::failure(ErrorCode::DEVICE_OPEN_FAILED,
                                        "IO device is not open");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registers_.find(address);
    if (it != registers_.end()) {
        return Result<uint16_t>::success(it->second);
    }
    return Result<uint16_t>::success(0);  // default to 0 for unset registers
}

} // namespace vxl
