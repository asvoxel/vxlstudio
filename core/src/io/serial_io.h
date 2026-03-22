#pragma once

#include "vxl/device.h"

#include <string>

namespace vxl {

// ---------------------------------------------------------------------------
// SerialIO -- serial-port text-protocol IO device
// Protocol:
//   set_output: sends "SET <pin> <0|1>\n"
//   get_input:  sends "GET <pin>\n", reads "0\n" or "1\n"
// ---------------------------------------------------------------------------
class SerialIO final : public IIO {
public:
    explicit SerialIO(const std::string& port_path, int baud_rate = 9600);
    ~SerialIO() override;

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
    std::string port_path_;
    int baud_rate_;
    int fd_ = -1;

    Result<void> write_line(const std::string& line);
    Result<std::string> read_line(int timeout_ms = 2000);
};

} // namespace vxl
