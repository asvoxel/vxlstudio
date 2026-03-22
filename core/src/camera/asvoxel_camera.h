// AsVoxel structured light camera driver
// TODO: Replace stub with actual SDK calls
//
// Integration steps:
// 1. Place AsVoxel SDK headers in 3rds/asvoxel_sdk/include/
// 2. Place AsVoxel SDK library in 3rds/asvoxel_sdk/lib/
// 3. Uncomment the SDK includes below
// 4. Implement each method using the SDK API
// 5. Update camera_factory.cpp to register this driver

#pragma once
#include "vxl/camera.h"
#include <string>
#include <vector>

// TODO: Uncomment when SDK is placed in 3rds/asvoxel_sdk/include/
// #include "asvoxel_sdk.h"

namespace vxl {

class AsVoxelCamera3D : public ICamera3D {
public:
    explicit AsVoxelCamera3D(const std::string& device_id);
    ~AsVoxelCamera3D() override;

    std::string device_id() const override;
    Result<void> open() override;
    void close() override;
    bool is_open() const override;
    Result<std::vector<Image>> capture_sequence() override;
    Result<void> set_exposure(int us) override;
    int exposure() const override;
    Result<void> set_fringe_count(int count) override;
    int fringe_count() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vxl
