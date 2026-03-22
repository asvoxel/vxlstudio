#pragma once

#include "vxl/export.h"
#include "vxl/error.h"
#include <cstdint>
#include <string>

namespace vxl {

// ---------------------------------------------------------------------------
// Phase 2-3: Device abstraction interfaces
// Camera interfaces are already in camera.h
// These are for non-camera devices
// ---------------------------------------------------------------------------

class VXL_EXPORT IProjector {
public:
    virtual ~IProjector() = default;
    virtual Result<void> project_pattern(int pattern_id) = 0;  // Phase 3
};

class VXL_EXPORT ITrigger {
public:
    virtual ~ITrigger() = default;
    virtual Result<void> wait_trigger(int timeout_ms = -1) = 0;
    virtual Result<void> send_trigger() = 0;
};

class VXL_EXPORT IIO {
public:
    virtual ~IIO() = default;
    virtual std::string device_id() const = 0;
    virtual Result<void> open() = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual Result<void> set_output(const std::string& pin, bool value) = 0;
    virtual Result<bool> get_input(const std::string& pin) = 0;
    virtual Result<void> write_register(int address, uint16_t value) = 0;
    virtual Result<uint16_t> read_register(int address) = 0;
};

class VXL_EXPORT IRobot {
public:
    virtual ~IRobot() = default;  // Phase 4
};

class VXL_EXPORT IPLC {
public:
    virtual ~IPLC() = default;  // Phase 4
};

} // namespace vxl
