#include "sim_camera.h"

#include <cmath>
#include <algorithm>

namespace vxl {

// ---------------------------------------------------------------------------
// Constants for the simulated scene
// ---------------------------------------------------------------------------
static constexpr double kPI = 3.14159265358979323846;

// Virtual surface: z = A * sin(2*pi*x/Lx) * cos(2*pi*y/Ly)
static constexpr double kSurfaceAmplitude  = 0.5;    // mm
// Lx, Ly are half the FOV -- computed dynamically from width/height.

// Fringe pattern parameters
static constexpr double kI0 = 128.0;  // DC bias
static constexpr double kIm =  90.0;  // modulation amplitude

// Gaussian noise sigma (in intensity counts)
static constexpr double kNoiseSigma = 2.0;

// 3 frequencies: 1, 8, 64 periods across the image width
static constexpr int kFrequencies[] = {1, 8, 64};
static constexpr int kNumFrequencies = 3;
static constexpr int kStepsPerFreq   = 4;  // phase shifts per frequency

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
SimCamera3D::SimCamera3D(int width, int height, int fringe_count,
                         const std::string& device_id)
    : device_id_(device_id), width_(width), height_(height),
      fringe_count_(fringe_count) {}

// ---------------------------------------------------------------------------
// ICamera interface
// ---------------------------------------------------------------------------
std::string SimCamera3D::device_id() const { return device_id_; }

Result<void> SimCamera3D::open() {
    open_ = true;
    return Result<void>::success();
}

void SimCamera3D::close() { open_ = false; }

bool SimCamera3D::is_open() const { return open_; }

// ---------------------------------------------------------------------------
// ICamera3D interface
// ---------------------------------------------------------------------------
Result<void> SimCamera3D::set_exposure(int us) {
    if (us <= 0) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Exposure must be positive");
    }
    exposure_us_ = us;
    return Result<void>::success();
}

int SimCamera3D::exposure() const { return exposure_us_; }

Result<void> SimCamera3D::set_fringe_count(int count) {
    if (count <= 0) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Fringe count must be positive");
    }
    fringe_count_ = count;
    return Result<void>::success();
}

int SimCamera3D::fringe_count() const { return fringe_count_; }

Result<std::vector<Image>> SimCamera3D::capture_sequence() {
    if (!open_) {
        return Result<std::vector<Image>>::failure(
            ErrorCode::DEVICE_OPEN_FAILED, "Camera is not open");
    }

    const int n_frames = fringe_count_;  // typically 12 = 3 freq x 4 steps
    std::vector<Image> frames;
    frames.reserve(n_frames);

    // Surface half-wavelengths (half the image dimension gives gentle hill)
    const double Lx = width_  / 2.0;
    const double Ly = height_ / 2.0;

    std::normal_distribution<double> noise(0.0, kNoiseSigma);

    for (int k = 0; k < n_frames; ++k) {
        Image img = Image::create(width_, height_, PixelFormat::GRAY8);
        uint8_t* data = img.buffer.data();

        // Determine which frequency and phase step this frame belongs to
        int freq_idx  = k / kStepsPerFreq;
        int step_idx  = k % kStepsPerFreq;

        // Wrap frequency index if fringe_count exceeds 3 frequencies
        double periods = 1.0;
        if (freq_idx < kNumFrequencies) {
            periods = static_cast<double>(kFrequencies[freq_idx]);
        } else {
            // Fallback: use the last frequency
            periods = static_cast<double>(kFrequencies[kNumFrequencies - 1]);
        }

        double phase_shift = step_idx * kPI / 2.0;  // 0, pi/2, pi, 3pi/2

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                // Virtual surface height at (x, y)
                double z = kSurfaceAmplitude
                           * std::sin(2.0 * kPI * x / Lx)
                           * std::cos(2.0 * kPI * y / Ly);

                // Projected fringe phase at column x
                double phi_proj = 2.0 * kPI * periods * x / width_;

                // Phase perturbation from surface height
                // (z modulates phase proportional to frequency)
                double phi_z = 2.0 * kPI * z * periods / width_;

                double intensity = kI0 + kIm * std::cos(
                    phi_proj + phase_shift - phi_z);

                // Add noise
                intensity += noise(rng_);

                // Clamp to [0, 255]
                int val = static_cast<int>(std::round(intensity));
                val = std::clamp(val, 0, 255);

                data[y * img.stride + x] = static_cast<uint8_t>(val);
            }
        }

        frames.push_back(std::move(img));
    }

    return Result<std::vector<Image>>::success(std::move(frames));
}

} // namespace vxl
