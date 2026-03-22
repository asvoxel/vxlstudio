#include "vxl/calibration.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vxl {

// ---------------------------------------------------------------------------
// JSON helpers (local)
// ---------------------------------------------------------------------------
namespace {

template <size_t N>
nlohmann::json array_to_json(const double (&arr)[N]) {
    nlohmann::json j = nlohmann::json::array();
    for (size_t i = 0; i < N; ++i) j.push_back(arr[i]);
    return j;
}

template <size_t N>
void json_to_array(const nlohmann::json& j, double (&arr)[N]) {
    for (size_t i = 0; i < N; ++i) arr[i] = j.at(i).get<double>();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------
Result<void> CalibrationParams::save(const std::string& path) const {
    nlohmann::json j;

    j["camera_matrix"]       = array_to_json(camera_matrix);
    j["camera_distortion"]   = array_to_json(camera_distortion);
    j["image_width"]         = image_width;
    j["image_height"]        = image_height;

    j["projector_matrix"]     = array_to_json(projector_matrix);
    j["projector_distortion"] = array_to_json(projector_distortion);
    j["projector_width"]      = projector_width;
    j["projector_height"]     = projector_height;

    j["rotation"]    = array_to_json(rotation);
    j["translation"] = array_to_json(translation);

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return Result<void>::failure(ErrorCode::IO_WRITE_FAILED,
                                    "Cannot open file for writing: " + path);
    }
    ofs << j.dump(4) << "\n";
    if (!ofs.good()) {
        return Result<void>::failure(ErrorCode::IO_WRITE_FAILED,
                                    "Failed to write calibration to: " + path);
    }
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
Result<CalibrationParams> CalibrationParams::load(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return Result<CalibrationParams>::failure(
            ErrorCode::FILE_NOT_FOUND,
            "Cannot open calibration file: " + path);
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const std::exception& e) {
        return Result<CalibrationParams>::failure(
            ErrorCode::INTERNAL_ERROR,
            std::string("JSON parse error: ") + e.what());
    }

    CalibrationParams p;

    try {
        json_to_array(j.at("camera_matrix"),       p.camera_matrix);
        json_to_array(j.at("camera_distortion"),   p.camera_distortion);
        p.image_width  = j.at("image_width").get<int>();
        p.image_height = j.at("image_height").get<int>();

        json_to_array(j.at("projector_matrix"),     p.projector_matrix);
        json_to_array(j.at("projector_distortion"), p.projector_distortion);
        p.projector_width  = j.at("projector_width").get<int>();
        p.projector_height = j.at("projector_height").get<int>();

        json_to_array(j.at("rotation"),    p.rotation);
        json_to_array(j.at("translation"), p.translation);
    } catch (const std::exception& e) {
        return Result<CalibrationParams>::failure(
            ErrorCode::INTERNAL_ERROR,
            std::string("Missing/invalid calibration field: ") + e.what());
    }

    return Result<CalibrationParams>::success(std::move(p));
}

// ---------------------------------------------------------------------------
// default_sim -- matches SimCamera3D geometry
// ---------------------------------------------------------------------------
CalibrationParams CalibrationParams::default_sim() {
    CalibrationParams p;

    // Camera: 1280x1024 sensor, ~50mm focal length equivalent
    // Focal length in pixels: fx = fy = focal_mm * image_width / sensor_width
    // Assuming sensor_width ~6.4mm -> fx = 50 * 1280 / 6.4 = 10000
    // Use a more typical industrial camera: fx ~ 2500 px
    const double fx_cam = 2500.0;
    const double fy_cam = 2500.0;
    p.image_width  = 1280;
    p.image_height = 1024;
    p.camera_matrix[0] = fx_cam;  p.camera_matrix[1] = 0.0;     p.camera_matrix[2] = 640.0;
    p.camera_matrix[3] = 0.0;    p.camera_matrix[4] = fy_cam;   p.camera_matrix[5] = 512.0;
    p.camera_matrix[6] = 0.0;    p.camera_matrix[7] = 0.0;      p.camera_matrix[8] = 1.0;

    // Zero distortion (simulated camera is ideal)
    for (int i = 0; i < 5; ++i) p.camera_distortion[i] = 0.0;

    // Projector: 1280x800
    const double fx_proj = 2500.0;
    const double fy_proj = 2500.0;
    p.projector_width  = 1280;
    p.projector_height = 800;
    // The projector principal point is offset from the camera's by 500 pixels
    // in X to model the convergent stereo geometry of a structured-light system.
    // With baseline T_x = 100 mm, this yields a working distance of
    //   Z = fx_proj * T_x / (cx_cam - cx_proj) = 2500 * 100 / 500 = 500 mm.
    // Using cx_proj == cx_cam would make the triangulation denominator vanish
    // for a flat reference surface, producing all-NaN 3D points.
    p.projector_matrix[0] = fx_proj;  p.projector_matrix[1] = 0.0;      p.projector_matrix[2] = 140.0;
    p.projector_matrix[3] = 0.0;     p.projector_matrix[4] = fy_proj;   p.projector_matrix[5] = 400.0;
    p.projector_matrix[6] = 0.0;     p.projector_matrix[7] = 0.0;       p.projector_matrix[8] = 1.0;

    for (int i = 0; i < 5; ++i) p.projector_distortion[i] = 0.0;

    // Identity rotation (camera and projector axes aligned)
    p.rotation[0] = 1.0; p.rotation[1] = 0.0; p.rotation[2] = 0.0;
    p.rotation[3] = 0.0; p.rotation[4] = 1.0; p.rotation[5] = 0.0;
    p.rotation[6] = 0.0; p.rotation[7] = 0.0; p.rotation[8] = 1.0;

    // Baseline: projector is 100mm to the right of the camera
    p.translation[0] = 100.0;
    p.translation[1] =   0.0;
    p.translation[2] =   0.0;

    return p;
}

} // namespace vxl
