#pragma once

#include "vxl/device.h"

#include <cstdint>
#include <string>

namespace vxl {

// ---------------------------------------------------------------------------
// ModbusIO -- Modbus TCP client implementation using BSD sockets
// ---------------------------------------------------------------------------
class ModbusIO final : public IIO {
public:
    explicit ModbusIO(const std::string& host, int port = 502);
    ~ModbusIO() override;

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
    std::string host_;
    int port_;
    int socket_fd_ = -1;
    uint16_t transaction_id_ = 0;

    // Parse pin name to numeric address (e.g., "Y0" -> 0, "X3" -> 3)
    static Result<int> parse_pin(const std::string& pin);

    // Low-level Modbus TCP frame exchange
    Result<std::vector<uint8_t>> send_request(uint8_t function_code,
                                              const std::vector<uint8_t>& payload);

    // Modbus functions
    Result<bool>     read_coil(int address);       // FC 01
    Result<void>     write_coil(int address, bool value);  // FC 05
    Result<uint16_t> read_holding_register(int address);   // FC 03
    Result<void>     write_holding_register(int address, uint16_t value);  // FC 06
};

} // namespace vxl
