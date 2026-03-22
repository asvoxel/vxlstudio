#pragma once

#include <random>
#include <string>
#include <vector>

#include "vxl/camera.h"

namespace vxl {

// ---------------------------------------------------------------------------
// SimCamera3D -- synthetic structured-light camera for testing / demos
// ---------------------------------------------------------------------------
class SimCamera3D final : public ICamera3D {
public:
    explicit SimCamera3D(int width = 1280, int height = 1024,
                         int fringe_count = 12,
                         const std::string& device_id = "SIM-001");

    // ICamera
    std::string  device_id() const override;
    Result<void> open() override;
    void         close() override;
    bool         is_open() const override;

    // ICamera3D
    Result<std::vector<Image>> capture_sequence() override;
    Result<void> set_exposure(int us) override;
    int          exposure() const override;
    Result<void> set_fringe_count(int count) override;
    int          fringe_count() const override;

private:
    std::string device_id_;
    int  width_;
    int  height_;
    int  fringe_count_;
    int  exposure_us_ = 5000;
    bool open_        = false;

    std::mt19937 rng_{42};
};

} // namespace vxl
