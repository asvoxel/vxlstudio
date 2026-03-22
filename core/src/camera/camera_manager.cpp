#include "vxl/camera_manager.h"

#include <algorithm>
#include <map>
#include <utility>

namespace vxl {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct CameraManager::Impl {
    // Ordered map so camera_ids() returns deterministic order.
    std::map<std::string, std::unique_ptr<ICamera3D>> cameras;
    std::map<std::string, CalibrationParams>           calibrations;

    // Dummy calibration returned when device_id not found (const-ref safe).
    CalibrationParams dummy_calib{};
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
CameraManager::CameraManager()  : impl_(std::make_unique<Impl>()) {}
CameraManager::~CameraManager() = default;

// ---------------------------------------------------------------------------
// add_camera
// ---------------------------------------------------------------------------
Result<void> CameraManager::add_camera(const std::string& device_id,
                                       const CalibrationParams& calib) {
    if (impl_->cameras.count(device_id)) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Camera already added: " + device_id);
    }

    auto res = Camera::open_3d(device_id);
    if (!res.ok()) {
        return Result<void>::failure(res.code, res.message);
    }

    impl_->cameras[device_id]      = std::move(res.value);
    impl_->calibrations[device_id] = calib;

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// remove_camera
// ---------------------------------------------------------------------------
Result<void> CameraManager::remove_camera(const std::string& device_id) {
    auto it = impl_->cameras.find(device_id);
    if (it == impl_->cameras.end()) {
        return Result<void>::failure(ErrorCode::DEVICE_NOT_FOUND,
                                    "Camera not found: " + device_id);
    }

    it->second->close();
    impl_->cameras.erase(it);
    impl_->calibrations.erase(device_id);

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// camera_ids
// ---------------------------------------------------------------------------
std::vector<std::string> CameraManager::camera_ids() const {
    std::vector<std::string> ids;
    ids.reserve(impl_->cameras.size());
    for (const auto& [id, _] : impl_->cameras) {
        ids.push_back(id);
    }
    return ids;
}

// ---------------------------------------------------------------------------
// get_camera
// ---------------------------------------------------------------------------
ICamera3D* CameraManager::get_camera(const std::string& device_id) {
    auto it = impl_->cameras.find(device_id);
    if (it == impl_->cameras.end()) return nullptr;
    return it->second.get();
}

// ---------------------------------------------------------------------------
// get_calibration
// ---------------------------------------------------------------------------
const CalibrationParams& CameraManager::get_calibration(
    const std::string& device_id) const {
    auto it = impl_->calibrations.find(device_id);
    if (it == impl_->calibrations.end()) {
        return impl_->dummy_calib;
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// capture_all
// ---------------------------------------------------------------------------
Result<std::unordered_map<std::string, std::vector<Image>>>
CameraManager::capture_all() {
    if (impl_->cameras.empty()) {
        return Result<std::unordered_map<std::string, std::vector<Image>>>::failure(
            ErrorCode::DEVICE_NOT_FOUND, "No cameras registered");
    }

    std::unordered_map<std::string, std::vector<Image>> results;

    for (auto& [id, cam] : impl_->cameras) {
        if (!cam->is_open()) {
            auto open_res = cam->open();
            if (!open_res.ok()) {
                return Result<std::unordered_map<std::string, std::vector<Image>>>::failure(
                    open_res.code,
                    "Failed to open camera " + id + ": " + open_res.message);
            }
        }

        auto cap_res = cam->capture_sequence();
        if (!cap_res.ok()) {
            return Result<std::unordered_map<std::string, std::vector<Image>>>::failure(
                cap_res.code,
                "Capture failed on camera " + id + ": " + cap_res.message);
        }

        results[id] = std::move(cap_res.value);
    }

    return Result<std::unordered_map<std::string, std::vector<Image>>>::success(
        std::move(results));
}

// ---------------------------------------------------------------------------
// aggregate_results
// ---------------------------------------------------------------------------
InspectionResult CameraManager::aggregate_results(
    const std::vector<std::pair<std::string, InspectionResult>>& per_camera_results) {

    InspectionResult combined;
    combined.ok = true;

    for (const auto& [cam_id, ir] : per_camera_results) {
        // Worst-case: any NG => overall NG.
        if (!ir.ok) {
            combined.ok = false;
        }

        // Merge defects, annotating source camera.
        for (auto defect : ir.defects) {
            defect.type = cam_id + "/" + defect.type;
            combined.defects.push_back(std::move(defect));
        }

        // Merge measures.
        for (const auto& mr : ir.measures) {
            combined.measures.push_back(mr);
        }

        // Keep latest timestamp.
        if (combined.timestamp.empty() || ir.timestamp > combined.timestamp) {
            combined.timestamp = ir.timestamp;
        }
    }

    return combined;
}

} // namespace vxl
