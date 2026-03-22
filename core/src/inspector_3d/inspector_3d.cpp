#include "vxl/inspector_3d.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vxl {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct Inspector3D::Impl {
    HeightMap reference;
    bool has_reference = false;
    std::vector<InspectorConfig> configs;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
Inspector3D::Inspector3D()  : impl_(std::make_unique<Impl>()) {}
Inspector3D::~Inspector3D() = default;

void Inspector3D::set_reference(const HeightMap& ref_hmap) {
    impl_->reference = ref_hmap;
    impl_->has_reference = true;
}

void Inspector3D::add_inspector(const InspectorConfig& config) {
    impl_->configs.push_back(config);
}

void Inspector3D::clear() {
    impl_->configs.clear();
    impl_->has_reference = false;
    impl_->reference = HeightMap{};
}

// ---------------------------------------------------------------------------
// Helper: ISO-8601 timestamp
// ---------------------------------------------------------------------------
static std::string now_iso8601() {
    auto tp = std::chrono::system_clock::now();
    auto t  = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
Result<InspectionResult> Inspector3D::run(const HeightMap& hmap) const {
    if (!hmap.buffer.data() || hmap.width <= 0 || hmap.height <= 0) {
        return Result<InspectionResult>::failure(ErrorCode::INVALID_PARAMETER,
                                                 "HeightMap is empty");
    }

    InspectionResult result;
    result.ok = true;
    result.timestamp = now_iso8601();

    for (const auto& cfg : impl_->configs) {
        InspectorResult ir;
        ir.name     = cfg.name;
        ir.type     = cfg.type;
        ir.severity = cfg.severity;
        ir.pass     = true;

        if (cfg.type == "ref_plane_fit") {
            // Fit plane on first ROI (or full image).
            ROI roi = cfg.rois.empty()
                ? ROI{0, 0, hmap.width, hmap.height}
                : cfg.rois.front();
            auto res = ref_plane_fit(hmap, roi);
            if (!res.ok()) {
                ir.pass = false;
                ir.message = res.message;
            } else {
                ir.message = "plane fit OK";
            }

        } else if (cfg.type == "height_measure") {
            double ref_h = 0.0;
            auto it = cfg.params.find("ref_height");
            if (it != cfg.params.end()) ref_h = it->second;

            double min_h = -1e30, max_h = 1e30;
            auto it_min = cfg.params.find("min_height_mm");
            if (it_min != cfg.params.end()) min_h = it_min->second;
            auto it_max = cfg.params.find("max_height_mm");
            if (it_max != cfg.params.end()) max_h = it_max->second;

            for (const auto& roi : cfg.rois) {
                auto res = height_measure(hmap, roi, ref_h);
                if (!res.ok()) {
                    ir.pass = false;
                    ir.message = res.message;
                    break;
                }
                // Check limits.
                const auto& mr = res.value;
                if (mr.avg_height < min_h || mr.avg_height > max_h) {
                    ir.pass = false;
                    ir.message = "Height out of range";
                }
                ir.measure = mr;
                result.measures.push_back(mr);
            }

        } else if (cfg.type == "flatness") {
            double max_flat = 1e30;
            auto it = cfg.params.find("max_flatness_mm");
            if (it != cfg.params.end()) max_flat = it->second;

            for (const auto& roi : cfg.rois) {
                auto res = flatness(hmap, roi);
                if (!res.ok()) {
                    ir.pass = false;
                    ir.message = res.message;
                    break;
                }
                if (res.value > max_flat) {
                    ir.pass = false;
                    ir.message = "Flatness exceeds limit";
                }
                ir.measure.max_height = res.value;
            }

        } else if (cfg.type == "height_threshold") {
            float th_min = 0.0f, th_max = 1e30f;
            auto it_max = cfg.params.find("max_height_mm");
            if (it_max != cfg.params.end()) th_max = static_cast<float>(it_max->second);
            auto it_min = cfg.params.find("min_height_mm");
            if (it_min != cfg.params.end()) th_min = static_cast<float>(it_min->second);

            auto mask_res = height_threshold(hmap, th_min, th_max);
            if (!mask_res.ok()) {
                ir.pass = false;
                ir.message = mask_res.message;
            } else {
                // If defect_cluster params present, cluster.
                int min_area = 10;
                auto it_area = cfg.params.find("min_defect_area_pixels");
                if (it_area != cfg.params.end())
                    min_area = static_cast<int>(it_area->second);

                auto dc_res = defect_cluster(mask_res.value,
                                              hmap.resolution_mm, min_area);
                if (dc_res.ok() && !dc_res.value.empty()) {
                    ir.defects = dc_res.value;
                    ir.pass = false;
                    ir.message = "Defects found";
                    for (auto& d : ir.defects) {
                        result.defects.push_back(d);
                    }
                }
            }

        } else if (cfg.type == "defect_cluster") {
            // Expects a prior threshold step; skip if standalone.
            ir.message = "defect_cluster should be used via height_threshold";

        } else if (cfg.type == "coplanarity") {
            double max_coplanarity = 1e30;
            auto it = cfg.params.find("max_coplanarity_mm");
            if (it != cfg.params.end()) max_coplanarity = it->second;

            auto res = coplanarity(hmap, cfg.rois);
            if (!res.ok()) {
                ir.pass = false;
                ir.message = res.message;
            } else {
                ir.measure.max_height = res.value;
                if (res.value > max_coplanarity) {
                    ir.pass = false;
                    ir.message = "Coplanarity exceeds limit";
                }
            }

        } else if (cfg.type == "template_compare") {
            if (!impl_->has_reference) {
                ir.pass = false;
                ir.message = "No reference height map set for template_compare";
            } else {
                float th = 0.1f;
                auto it_th = cfg.params.find("threshold_mm");
                if (it_th != cfg.params.end()) th = static_cast<float>(it_th->second);

                int min_area = 10;
                auto it_area = cfg.params.find("min_defect_area_pixels");
                if (it_area != cfg.params.end())
                    min_area = static_cast<int>(it_area->second);

                auto res = template_compare(hmap, impl_->reference, th, min_area);
                if (!res.ok()) {
                    ir.pass = false;
                    ir.message = res.message;
                } else {
                    const auto& cr = res.value;
                    if (!cr.defects.empty()) {
                        ir.pass = false;
                        ir.message = "Template compare found defects";
                        for (auto& d : cr.defects) {
                            result.defects.push_back(d);
                        }
                        ir.defects = cr.defects;
                    }
                    ir.measure.max_height = cr.max_diff;
                    ir.measure.avg_height = cr.mean_diff;
                }
            }

        } else {
            ir.pass = false;
            ir.message = "Unknown inspector type: " + cfg.type;
        }

        // Apply severity-based judgement.
        if (!ir.pass && cfg.severity == "critical") {
            result.ok = false;
        }

        // Store (we embed InspectorResult data into InspectionResult fields).
        // The per-inspector data is serialized into to_json() via measures/defects
        // which are already appended above.
    }

    return Result<InspectionResult>::success(std::move(result));
}

} // namespace vxl
