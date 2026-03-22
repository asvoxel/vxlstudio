#include "vxl/camera.h"
#include "sim_camera.h"
#include "asvoxel_camera.h"

namespace vxl {
namespace Camera {

std::vector<std::string> enumerate() {
    std::vector<std::string> devices;

    // Simulated camera is always available
    devices.push_back("SIM-001");

    // TODO: When AsVoxel SDK is integrated, probe for real devices:
    //   int n = asvoxel_sdk_enumerate(nullptr, 0);
    //   for (int i = 0; i < n; ++i) {
    //       char id[64];
    //       asvoxel_sdk_get_device_id(i, id, sizeof(id));
    //       devices.push_back(std::string("ASV-") + id);
    //   }

    return devices;
}

Result<std::unique_ptr<ICamera3D>> open_3d(const std::string& device_id) {
    // Accept any device_id starting with "SIM" as a simulated camera.
    if (device_id.rfind("SIM", 0) == 0) {
        auto cam = std::make_unique<SimCamera3D>(1280, 1024, 12, device_id);
        auto r = cam->open();
        if (!r.ok()) {
            return Result<std::unique_ptr<ICamera3D>>::failure(
                r.code, std::move(r.message));
        }
        return Result<std::unique_ptr<ICamera3D>>::success(std::move(cam));
    }

    // Accept any device_id starting with "ASV-" as an AsVoxel camera.
    if (device_id.rfind("ASV-", 0) == 0) {
        auto cam = std::make_unique<AsVoxelCamera3D>(device_id);
        auto r = cam->open();
        if (!r.ok()) {
            return Result<std::unique_ptr<ICamera3D>>::failure(
                r.code, std::move(r.message));
        }
        return Result<std::unique_ptr<ICamera3D>>::success(std::move(cam));
    }

    return Result<std::unique_ptr<ICamera3D>>::failure(
        ErrorCode::DEVICE_NOT_FOUND,
        "No device with id: " + device_id);
}

Result<std::unique_ptr<ICamera2D>> open_2d(const std::string& device_id) {
    // No 2D simulator implemented yet.
    return Result<std::unique_ptr<ICamera2D>>::failure(
        ErrorCode::DEVICE_NOT_FOUND,
        "No 2D device with id: " + device_id);
}

} // namespace Camera
} // namespace vxl
