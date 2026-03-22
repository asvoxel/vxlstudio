#pragma once

#include "vxl/device.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace vxl {

// ---------------------------------------------------------------------------
// SimIO -- simulated IO device for testing (loopback)
// ---------------------------------------------------------------------------
class SimIO final : public IIO {
public:
    SimIO() = default;

    // IIO
    std::string  device_id() const override;
    Result<void> open() override;
    void         close() override;
    bool         is_open() const override;
    Result<void> set_output(const std::string& pin, bool value) override;
    Result<bool> get_input(const std::string& pin) override;
    Result<void> write_register(int address, uint16_t value) override;
    Result<uint16_t> read_register(int address) override;

private:
    bool open_ = false;
    std::mutex mutex_;
    std::map<std::string, bool> pins_;
    std::map<int, uint16_t> registers_;
};

} // namespace vxl
