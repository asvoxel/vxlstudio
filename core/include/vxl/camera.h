#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ---------------------------------------------------------------------------
// ICamera -- abstract base for all cameras
// ---------------------------------------------------------------------------
class VXL_EXPORT ICamera {
public:
    virtual ~ICamera() = default;

    virtual std::string device_id() const = 0;
    virtual Result<void> open() = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
};

// ---------------------------------------------------------------------------
// ICamera2D -- 2-D (single-frame) camera
// ---------------------------------------------------------------------------
class VXL_EXPORT ICamera2D : public ICamera {
public:
    virtual Result<Image> capture() = 0;
    virtual Result<void> set_exposure(int us) = 0;
    virtual int exposure() const = 0;
};

// ---------------------------------------------------------------------------
// ICamera3D -- structured-light 3-D camera
// ---------------------------------------------------------------------------
class VXL_EXPORT ICamera3D : public ICamera {
public:
    virtual Result<std::vector<Image>> capture_sequence() = 0;
    virtual Result<void> set_exposure(int us) = 0;
    virtual int exposure() const = 0;
    virtual Result<void> set_fringe_count(int count) = 0;
    virtual int fringe_count() const = 0;
};

// ---------------------------------------------------------------------------
// Camera factory functions
// ---------------------------------------------------------------------------
namespace Camera {

VXL_EXPORT Result<std::unique_ptr<ICamera3D>> open_3d(const std::string& device_id);
VXL_EXPORT Result<std::unique_ptr<ICamera2D>> open_2d(const std::string& device_id);
VXL_EXPORT std::vector<std::string> enumerate();

} // namespace Camera

} // namespace vxl
