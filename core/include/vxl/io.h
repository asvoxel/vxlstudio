#pragma once

#include "vxl/export.h"
#include "vxl/error.h"
#include "vxl/device.h"
#include <memory>
#include <string>
#include <vector>

namespace vxl {

// ---------------------------------------------------------------------------
// IOManager -- factory for IO devices
// URI formats:
//   "modbus-tcp://192.168.1.10:502"
//   "serial:///dev/ttyUSB0:9600"
//   "sim://io"
// ---------------------------------------------------------------------------
class VXL_EXPORT IOManager {
public:
    static Result<std::unique_ptr<IIO>> open(const std::string& uri);
    static std::vector<std::string> enumerate();
};

} // namespace vxl
