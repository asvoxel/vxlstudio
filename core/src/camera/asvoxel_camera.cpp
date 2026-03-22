// AsVoxel structured light camera driver -- stub implementation.
//
// Each method returns DEVICE_NOT_FOUND until the real SDK is integrated.
// See asvoxel_camera.h for step-by-step integration instructions.

#include "asvoxel_camera.h"

namespace vxl {

// ---------------------------------------------------------------------------
// Private implementation (pimpl)
// ---------------------------------------------------------------------------
struct AsVoxelCamera3D::Impl {
    std::string device_id;
    bool is_open = false;
    int exposure_us = 5000;
    int fringe_count = 12;

    // TODO: Add SDK handle here, e.g.:
    //   asvoxel_handle_t handle = nullptr;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
AsVoxelCamera3D::AsVoxelCamera3D(const std::string& device_id)
    : impl_(std::make_unique<Impl>())
{
    impl_->device_id = device_id;
}

AsVoxelCamera3D::~AsVoxelCamera3D() {
    close();
}

// ---------------------------------------------------------------------------
// ICamera interface
// ---------------------------------------------------------------------------
std::string AsVoxelCamera3D::device_id() const {
    return impl_->device_id;
}

Result<void> AsVoxelCamera3D::open() {
    // TODO: Replace with actual SDK call:
    //   auto handle = asvoxel_sdk_open(impl_->device_id.c_str());
    //   if (!handle) return Result<void>::failure(ErrorCode::DEVICE_OPEN_FAILED, ...);
    //   impl_->handle = handle;
    //   impl_->is_open = true;
    //   return Result<void>::success();
    return Result<void>::failure(ErrorCode::DEVICE_NOT_FOUND,
        "AsVoxel SDK not integrated. See asvoxel_camera.h for steps.");
}

void AsVoxelCamera3D::close() {
    // TODO: Replace with actual SDK call:
    //   if (impl_->handle) {
    //       asvoxel_sdk_close(impl_->handle);
    //       impl_->handle = nullptr;
    //   }
    impl_->is_open = false;
}

bool AsVoxelCamera3D::is_open() const {
    return impl_->is_open;
}

// ---------------------------------------------------------------------------
// ICamera3D interface
// ---------------------------------------------------------------------------
Result<std::vector<Image>> AsVoxelCamera3D::capture_sequence() {
    // TODO: Replace with actual SDK call:
    //   if (!impl_->is_open)
    //       return failure(DEVICE_OPEN_FAILED, "Camera not open");
    //   int n_frames = impl_->fringe_count;
    //   std::vector<Image> frames;
    //   frames.reserve(n_frames);
    //   for (int k = 0; k < n_frames; ++k) {
    //       asvoxel_frame_t raw = asvoxel_sdk_capture_frame(impl_->handle);
    //       Image img = Image::create(raw.width, raw.height, PixelFormat::GRAY8);
    //       std::memcpy(img.buffer.data(), raw.data, raw.width * raw.height);
    //       asvoxel_sdk_release_frame(raw);
    //       frames.push_back(std::move(img));
    //   }
    //   return Result<std::vector<Image>>::success(std::move(frames));
    return Result<std::vector<Image>>::failure(ErrorCode::DEVICE_NOT_FOUND,
        "AsVoxel SDK not integrated. See asvoxel_camera.h for steps.");
}

Result<void> AsVoxelCamera3D::set_exposure(int us) {
    // TODO: Replace with actual SDK call:
    //   int err = asvoxel_sdk_set_exposure(impl_->handle, us);
    //   if (err != 0) return failure(INVALID_PARAMETER, ...);
    //   impl_->exposure_us = us;
    //   return Result<void>::success();
    if (us <= 0) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
            "Exposure must be positive");
    }
    impl_->exposure_us = us;
    return Result<void>::failure(ErrorCode::DEVICE_NOT_FOUND,
        "AsVoxel SDK not integrated. See asvoxel_camera.h for steps.");
}

int AsVoxelCamera3D::exposure() const {
    return impl_->exposure_us;
}

Result<void> AsVoxelCamera3D::set_fringe_count(int count) {
    // TODO: Replace with actual SDK call:
    //   int err = asvoxel_sdk_set_pattern_count(impl_->handle, count);
    //   if (err != 0) return failure(INVALID_PARAMETER, ...);
    //   impl_->fringe_count = count;
    //   return Result<void>::success();
    if (count <= 0) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
            "Fringe count must be positive");
    }
    impl_->fringe_count = count;
    return Result<void>::failure(ErrorCode::DEVICE_NOT_FOUND,
        "AsVoxel SDK not integrated. See asvoxel_camera.h for steps.");
}

int AsVoxelCamera3D::fringe_count() const {
    return impl_->fringe_count;
}

} // namespace vxl
