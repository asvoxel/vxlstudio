#include "vxl/io.h"

#include "modbus_io.h"
#include "serial_io.h"
#include "sim_io.h"

#include <algorithm>

#ifndef _WIN32
    #include <dirent.h>
    #include <sys/stat.h>
#endif

namespace vxl {

// ---------------------------------------------------------------------------
// URI parsing helpers
// ---------------------------------------------------------------------------
static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// Parse "modbus-tcp://host:port" -> (host, port)
static Result<std::pair<std::string, int>> parse_modbus_uri(const std::string& uri) {
    // uri = "modbus-tcp://host:port"
    const std::string prefix = "modbus-tcp://";
    std::string rest = uri.substr(prefix.size());

    auto colon = rest.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        return Result<std::pair<std::string, int>>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Invalid modbus-tcp URI: " + uri + " (expected modbus-tcp://host:port)");
    }

    std::string host = rest.substr(0, colon);
    int port = 502;
    try {
        port = std::stoi(rest.substr(colon + 1));
    } catch (...) {
        return Result<std::pair<std::string, int>>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Invalid port in URI: " + uri);
    }

    return Result<std::pair<std::string, int>>::success({host, port});
}

// Parse "serial:///dev/ttyUSB0:9600" -> (path, baud)
static Result<std::pair<std::string, int>> parse_serial_uri(const std::string& uri) {
    const std::string prefix = "serial://";
    std::string rest = uri.substr(prefix.size());

    // Find last colon for baud rate separator
    auto colon = rest.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        // No baud rate specified; use default
        return Result<std::pair<std::string, int>>::success({rest, 9600});
    }

    std::string path = rest.substr(0, colon);
    int baud = 9600;
    try {
        baud = std::stoi(rest.substr(colon + 1));
    } catch (...) {
        // If the part after colon isn't a number, treat entire rest as path
        return Result<std::pair<std::string, int>>::success({rest, 9600});
    }

    return Result<std::pair<std::string, int>>::success({path, baud});
}

// ---------------------------------------------------------------------------
// Detect serial ports on the system
// ---------------------------------------------------------------------------
static std::vector<std::string> detect_serial_ports() {
    std::vector<std::string> ports;

#ifdef __APPLE__
    // macOS: /dev/cu.* and /dev/tty.usb*
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (starts_with(name, "cu.usb") || starts_with(name, "tty.usb")) {
                ports.push_back("serial:///dev/" + name + ":9600");
            }
        }
        closedir(dir);
    }
#elif defined(__linux__)
    // Linux: /dev/ttyUSB* and /dev/ttyACM*
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (starts_with(name, "ttyUSB") || starts_with(name, "ttyACM")) {
                ports.push_back("serial:///dev/" + name + ":9600");
            }
        }
        closedir(dir);
    }
#endif
    // Windows: not implemented in this version

    std::sort(ports.begin(), ports.end());
    return ports;
}

// ---------------------------------------------------------------------------
// IOManager implementation
// ---------------------------------------------------------------------------
Result<std::unique_ptr<IIO>> IOManager::open(const std::string& uri) {
    if (starts_with(uri, "sim://")) {
        auto dev = std::make_unique<SimIO>();
        auto r = dev->open();
        if (!r.ok()) {
            return Result<std::unique_ptr<IIO>>::failure(r.code, r.message);
        }
        return Result<std::unique_ptr<IIO>>::success(std::move(dev));
    }

    if (starts_with(uri, "modbus-tcp://")) {
        auto parsed = parse_modbus_uri(uri);
        if (!parsed.ok()) {
            return Result<std::unique_ptr<IIO>>::failure(parsed.code, parsed.message);
        }
        auto dev = std::make_unique<ModbusIO>(parsed.value.first, parsed.value.second);
        auto r = dev->open();
        if (!r.ok()) {
            return Result<std::unique_ptr<IIO>>::failure(r.code, r.message);
        }
        return Result<std::unique_ptr<IIO>>::success(std::move(dev));
    }

    if (starts_with(uri, "serial://")) {
        auto parsed = parse_serial_uri(uri);
        if (!parsed.ok()) {
            return Result<std::unique_ptr<IIO>>::failure(parsed.code, parsed.message);
        }
        auto dev = std::make_unique<SerialIO>(parsed.value.first, parsed.value.second);
        auto r = dev->open();
        if (!r.ok()) {
            return Result<std::unique_ptr<IIO>>::failure(r.code, r.message);
        }
        return Result<std::unique_ptr<IIO>>::success(std::move(dev));
    }

    return Result<std::unique_ptr<IIO>>::failure(
        ErrorCode::INVALID_PARAMETER,
        "Unknown IO URI scheme: " + uri);
}

std::vector<std::string> IOManager::enumerate() {
    std::vector<std::string> devices;

    // Simulator is always available
    devices.push_back("sim://io");

    // Detect serial ports
    auto serial_ports = detect_serial_ports();
    devices.insert(devices.end(), serial_ports.begin(), serial_ports.end());

    return devices;
}

} // namespace vxl
