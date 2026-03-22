#include "vxl/pipeline.h"

#include "vxl/camera.h"
#include "vxl/calibration.h"
#include "vxl/height_map.h"
#include "vxl/inference.h"
#include "vxl/inspector_2d.h"
#include "vxl/inspector_3d.h"
#include "vxl/recipe.h"
#include "vxl/reconstruct.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace vxl {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers: StepType <-> string conversion
// ---------------------------------------------------------------------------
static const char* step_type_to_string(StepType t) {
    switch (t) {
        case StepType::CAPTURE:      return "capture";
        case StepType::RECONSTRUCT:  return "reconstruct";
        case StepType::INSPECT_3D:   return "inspect_3d";
        case StepType::INSPECT_2D:   return "inspect_2d";
        case StepType::OUTPUT:       return "output";
        case StepType::CUSTOM:       return "custom";
    }
    return "custom";
}

static Result<StepType> string_to_step_type(const std::string& s) {
    if (s == "capture")      return Result<StepType>::success(StepType::CAPTURE);
    if (s == "reconstruct")  return Result<StepType>::success(StepType::RECONSTRUCT);
    if (s == "inspect_3d")   return Result<StepType>::success(StepType::INSPECT_3D);
    if (s == "inspect_2d")   return Result<StepType>::success(StepType::INSPECT_2D);
    if (s == "output")       return Result<StepType>::success(StepType::OUTPUT);
    if (s == "custom")       return Result<StepType>::success(StepType::CUSTOM);
    return Result<StepType>::failure(ErrorCode::INVALID_PARAMETER,
                                     "Unknown step type: " + s);
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct Pipeline::Impl {
    std::vector<PipelineStep> steps;

    // Configuration
    std::string       camera_id;
    CalibrationParams calibration{};
    bool              calibration_set = false;
    std::string       recipe_path;

    // Custom callbacks keyed by step name
    std::unordered_map<std::string, CustomStepCallback> custom_callbacks;

    // Continuous-mode state
    std::atomic<bool>  running{false};
    std::thread        worker;
    std::mutex         mutex;   // protects start/stop sequencing

    // Cached camera (opened once, reused)
    std::unique_ptr<ICamera3D> camera_3d;

    // ------ step executors ---------------------------------------------------

    Result<void> exec_capture(PipelineContext& ctx) {
        if (!camera_3d) {
            auto cam_res = Camera::open_3d(camera_id);
            if (!cam_res.ok()) return Result<void>::failure(cam_res.code, cam_res.message);
            camera_3d = std::move(cam_res.value);
        }

        if (!camera_3d->is_open()) {
            auto open_res = camera_3d->open();
            if (!open_res.ok()) return Result<void>::failure(open_res.code, open_res.message);
        }

        auto seq = camera_3d->capture_sequence();
        if (!seq.ok()) return Result<void>::failure(seq.code, seq.message);
        ctx.frames = std::move(seq.value);

        spdlog::debug("Pipeline CAPTURE: {} frames acquired", ctx.frames.size());
        return Result<void>::success();
    }

    Result<void> exec_reconstruct(PipelineContext& ctx,
                                   const PipelineStep& step) {
        const CalibrationParams& calib = calibration_set
            ? calibration
            : CalibrationParams::default_sim();

        ReconstructParams rp;
        // Override method if params specify it
        auto it = step.params.find("method");
        if (it != step.params.end()) {
            rp.method = it->second;
        }

        // Try Reconstruct::process() generic dispatch first when a provider
        // type is given; fall back to from_fringe for structured_light or
        // when no specific provider is requested.
        Result<ReconstructOutput> rr = Result<ReconstructOutput>::failure(
            ErrorCode::INTERNAL_ERROR, "not executed");

        std::string provider = (it != step.params.end()) ? it->second : "structured_light";

        if (provider == "structured_light" || provider == "multi_frequency" ||
            provider == "gray_code") {
            rr = Reconstruct::from_fringe(ctx.frames, calib, rp);
        } else if (provider == "depth" && !ctx.frames.empty()) {
            rr = Reconstruct::from_depth(ctx.frames.front(), calib);
        } else {
            rr = Reconstruct::process(provider, ctx.frames, calib, rp);
        }

        if (!rr.ok()) return Result<void>::failure(rr.code, rr.message);

        ctx.height_map  = std::move(rr.value.height_map);
        ctx.point_cloud = std::move(rr.value.point_cloud);

        spdlog::debug("Pipeline RECONSTRUCT: height_map {}x{}, {} points",
                      ctx.height_map.width, ctx.height_map.height,
                      ctx.point_cloud.point_count);
        return Result<void>::success();
    }

    Result<void> exec_inspect_3d(PipelineContext& ctx) {
        if (recipe_path.empty()) {
            return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                        "INSPECT_3D step requires a recipe path");
        }

        auto rr = Recipe::load(recipe_path);
        if (!rr.ok()) return Result<void>::failure(rr.code, rr.message);

        // --- Reference plane subtraction ---
        // If the recipe defines a reference plane ROI, fit a plane to
        // that region and subtract it from the height map so that
        // inspectors see relative surface heights (~0 mm baseline)
        // instead of absolute Z coordinates (~500 mm working distance).
        HeightMap inspect_hmap = ctx.height_map;

        if (rr.value.has_reference_plane()) {
            ROI ref_roi = rr.value.ref_plane_roi();

            auto plane_res = HeightMapProcessor::fit_reference_plane(
                ctx.height_map, ref_roi);
            if (plane_res.ok()) {
                auto sub_res = HeightMapProcessor::subtract_reference(
                    ctx.height_map, plane_res.value);
                if (sub_res.ok()) {
                    inspect_hmap = std::move(sub_res.value);
                    spdlog::debug(
                        "Pipeline INSPECT_3D: subtracted reference plane "
                        "(ROI [{},{} {}x{}], plane a={:.4f} b={:.4f} c={:.4f} d={:.4f})",
                        ref_roi.x, ref_roi.y, ref_roi.w, ref_roi.h,
                        plane_res.value.a, plane_res.value.b,
                        plane_res.value.c, plane_res.value.d);
                } else {
                    spdlog::warn(
                        "Pipeline INSPECT_3D: subtract_reference failed: {}; "
                        "proceeding with absolute heights",
                        sub_res.message);
                }
            } else {
                spdlog::warn(
                    "Pipeline INSPECT_3D: fit_reference_plane failed: {}; "
                    "proceeding with absolute heights",
                    plane_res.message);
            }
        }

        auto ir = rr.value.inspect(inspect_hmap);
        if (!ir.ok()) return Result<void>::failure(ir.code, ir.message);

        ctx.result = std::move(ir.value);

        spdlog::debug("Pipeline INSPECT_3D: ok={} defects={}",
                      ctx.result.ok, ctx.result.defects.size());
        return Result<void>::success();
    }

    Result<void> exec_inspect_2d(PipelineContext& ctx,
                                  const PipelineStep& step) {
        if (ctx.frames.empty()) {
            return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                        "INSPECT_2D step requires at least one frame");
        }

        const Image& img = ctx.frames.front();

        auto ops_it = step.params.find("operators");
        if (ops_it == step.params.end() || ops_it->second.empty()) {
            spdlog::debug("Pipeline INSPECT_2D: no operators specified, skipping");
            return Result<void>::success();
        }

        const std::string& ops = ops_it->second;

        if (ops.find("anomaly_detect") != std::string::npos) {
            // Run anomaly detection (with or without a model)
            float threshold = 0.5f;
            auto thr_it = step.params.find("threshold");
            if (thr_it != step.params.end()) {
                try { threshold = std::stof(thr_it->second); } catch (...) {}
            }

            // Try to load model if model_path is given
            Inference* model_ptr = nullptr;
            Inference model;
            auto model_it = step.params.find("model_path");
            if (model_it != step.params.end() && !model_it->second.empty()) {
                auto mr = Inference::load(model_it->second);
                if (mr.ok()) {
                    model = std::move(mr.value);
                    model_ptr = &model;
                } else {
                    spdlog::warn("Pipeline: could not load anomaly model: {}",
                                 mr.message);
                }
            }

            auto ar = Inspector2D::anomaly_detect(img, model_ptr, threshold);
            if (ar.ok()) {
                float score = ar.value;
                ctx.custom_data["anomaly_score"] = score;
                if (score >= threshold) {
                    ctx.result.ok = false;
                    DefectRegion d;
                    d.type = "anomaly";
                    d.area_mm2 = score;
                    ctx.result.defects.push_back(d);
                }
                spdlog::debug("Pipeline INSPECT_2D anomaly_detect: score={}",
                              score);
            } else {
                spdlog::warn("Pipeline INSPECT_2D anomaly_detect failed: {}",
                             ar.message);
            }
        }

        if (ops.find("edge_detect") != std::string::npos) {
            auto er = Inspector2D::edge_detect(img);
            if (er.ok()) {
                ctx.custom_data["edge_image"] = std::move(er.value);
                spdlog::debug("Pipeline INSPECT_2D edge_detect: done");
            }
        }

        if (ops.find("blob_analysis") != std::string::npos) {
            auto br = Inspector2D::blob_analysis(img);
            if (br.ok()) {
                ctx.custom_data["blobs"] = std::move(br.value);
                spdlog::debug("Pipeline INSPECT_2D blob_analysis: {} blobs",
                              std::any_cast<std::vector<BlobResult>>(
                                  ctx.custom_data["blobs"]).size());
            }
        }

        return Result<void>::success();
    }

    Result<void> exec_output(PipelineContext& ctx,
                              const PipelineStep& step) {
        bool save_ng = false;
        auto it = step.params.find("save_ng");
        if (it != step.params.end() && it->second == "true") {
            save_ng = true;
        }

        if (save_ng && !ctx.result.ok) {
            spdlog::info("Pipeline OUTPUT: NG result -- {} defects, recipe={}",
                         ctx.result.defects.size(), ctx.result.recipe_name);
            // In a real system this would save images / trigger IO.
            // For now we log the event.
        } else {
            spdlog::debug("Pipeline OUTPUT: result ok={}", ctx.result.ok);
        }

        return Result<void>::success();
    }

    Result<void> exec_custom(PipelineContext& ctx,
                              const PipelineStep& step) {
        auto it = custom_callbacks.find(step.name);
        if (it == custom_callbacks.end()) {
            return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                        "No callback registered for custom step: " + step.name);
        }
        return it->second(ctx);
    }
};

// ---------------------------------------------------------------------------
// Construction / destruction / move
// ---------------------------------------------------------------------------
Pipeline::Pipeline()  : impl_(std::make_unique<Impl>()) {}
Pipeline::~Pipeline() {
    if (impl_ && impl_->running.load()) {
        stop();
    }
}
Pipeline::Pipeline(Pipeline&&) noexcept            = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
Result<Pipeline> Pipeline::load(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return Result<Pipeline>::failure(ErrorCode::FILE_NOT_FOUND,
                                        "Cannot open pipeline file: " + path);
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        return Result<Pipeline>::failure(ErrorCode::INVALID_PARAMETER,
                                        std::string("JSON parse error: ") + e.what());
    }

    Pipeline p;

    p.impl_->camera_id  = j.value("camera", "");
    p.impl_->recipe_path = j.value("recipe", "");

    // Optionally load calibration inline
    if (j.contains("calibration")) {
        const auto& cv = j["calibration"];
        if (cv.is_string()) {
            // It is a path -- we do not load it here; user should call
            // set_calibration() separately.  Store path in custom data.
            // We leave calibration_set = false.
        }
    }

    if (!j.contains("steps") || !j["steps"].is_array()) {
        return Result<Pipeline>::failure(ErrorCode::INVALID_PARAMETER,
                                        "Pipeline JSON must contain a 'steps' array");
    }

    for (const auto& sj : j["steps"]) {
        PipelineStep step;
        auto type_str = sj.value("type", "");
        auto tr = string_to_step_type(type_str);
        if (!tr.ok()) return Result<Pipeline>::failure(tr.code, tr.message);
        step.type = tr.value;
        step.name = sj.value("name", "");

        if (sj.contains("params") && sj["params"].is_object()) {
            for (auto& [key, val] : sj["params"].items()) {
                if (val.is_string()) {
                    step.params[key] = val.get<std::string>();
                } else if (val.is_boolean()) {
                    step.params[key] = val.get<bool>() ? "true" : "false";
                } else {
                    step.params[key] = val.dump();
                }
            }
        }

        p.impl_->steps.push_back(std::move(step));
    }

    return Result<Pipeline>::success(std::move(p));
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------
Result<void> Pipeline::save(const std::string& path) const {
    json j;
    j["version"] = "1.0";
    j["camera"]  = impl_->camera_id;
    j["recipe"]  = impl_->recipe_path;

    json steps_arr = json::array();
    for (const auto& s : impl_->steps) {
        json sj;
        sj["type"] = step_type_to_string(s.type);
        sj["name"] = s.name;
        if (!s.params.empty()) {
            json pj;
            for (const auto& [k, v] : s.params) {
                pj[k] = v;
            }
            sj["params"] = pj;
        }
        steps_arr.push_back(sj);
    }
    j["steps"] = steps_arr;

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return Result<void>::failure(ErrorCode::IO_WRITE_FAILED,
                                    "Cannot write pipeline file: " + path);
    }
    ofs << j.dump(2) << "\n";

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Builder API
// ---------------------------------------------------------------------------
void Pipeline::add_step(const PipelineStep& step) {
    impl_->steps.push_back(step);
}

void Pipeline::set_custom_callback(const std::string& step_name,
                                    CustomStepCallback cb) {
    impl_->custom_callbacks[step_name] = std::move(cb);
}

void Pipeline::set_camera_id(const std::string& id) {
    impl_->camera_id = id;
}

void Pipeline::set_calibration(const CalibrationParams& calib) {
    impl_->calibration     = calib;
    impl_->calibration_set = true;
}

void Pipeline::set_recipe(const std::string& recipe_path) {
    impl_->recipe_path = recipe_path;
}

// ---------------------------------------------------------------------------
// run_once
// ---------------------------------------------------------------------------
Result<PipelineContext> Pipeline::run_once() {
    if (impl_->steps.empty()) {
        return Result<PipelineContext>::failure(
            ErrorCode::INVALID_PARAMETER, "Pipeline has no steps");
    }

    PipelineContext ctx;

    for (size_t i = 0; i < impl_->steps.size(); ++i) {
        const auto& step = impl_->steps[i];
        Result<void> r = Result<void>::success();

        switch (step.type) {
            case StepType::CAPTURE:
                r = impl_->exec_capture(ctx);
                break;
            case StepType::RECONSTRUCT:
                r = impl_->exec_reconstruct(ctx, step);
                break;
            case StepType::INSPECT_3D:
                r = impl_->exec_inspect_3d(ctx);
                break;
            case StepType::INSPECT_2D:
                r = impl_->exec_inspect_2d(ctx, step);
                break;
            case StepType::OUTPUT:
                r = impl_->exec_output(ctx, step);
                break;
            case StepType::CUSTOM:
                r = impl_->exec_custom(ctx, step);
                break;
        }

        if (!r.ok()) {
            return Result<PipelineContext>::failure(
                r.code,
                "Step '" + step.name + "' (" +
                step_type_to_string(step.type) + ") failed: " + r.message);
        }
    }

    return Result<PipelineContext>::success(std::move(ctx));
}

// ---------------------------------------------------------------------------
// Continuous mode
// ---------------------------------------------------------------------------
void Pipeline::start(std::function<void(const PipelineContext&)> on_complete) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if (impl_->running.load()) return;  // already running

    impl_->running.store(true);
    impl_->worker = std::thread([this, cb = std::move(on_complete)]() {
        while (impl_->running.load()) {
            auto r = run_once();
            if (r.ok()) {
                if (cb) cb(r.value);
            } else {
                spdlog::error("Pipeline continuous loop error: {}", r.message);
                // Brief sleep to avoid busy-loop on persistent errors
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}

void Pipeline::stop() {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    impl_->running.store(false);
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }

    // Close camera to allow clean re-open on next start
    if (impl_->camera_3d) {
        impl_->camera_3d->close();
        impl_->camera_3d.reset();
    }
}

bool Pipeline::is_running() const {
    return impl_->running.load();
}

const std::vector<PipelineStep>& Pipeline::steps() const {
    return impl_->steps;
}

} // namespace vxl
