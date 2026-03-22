#include "vxl/recipe.h"

#include "vxl/inspector_3d.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vxl {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct Recipe::Impl {
    std::string                   version = "1.0";
    std::string                   name;
    std::string                   type;        // pcb_smt, flatness, ...
    std::string                   description;
    std::string                   created;
    std::string                   modified;

    // Reference.
    std::string                   ref_height_map_file;
    ROI                           ref_plane_roi{};

    // Inspectors.
    std::vector<InspectorConfig>  inspector_configs;

    // Judgement severity map (inspector_name -> severity).
    std::map<std::string, std::string> severity_map;
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
Recipe::Recipe()                           : impl_(std::make_shared<Impl>()) {}
Recipe::~Recipe()                          = default;
Recipe::Recipe(const Recipe&)              = default;
Recipe& Recipe::operator=(const Recipe&)   = default;
Recipe::Recipe(Recipe&&) noexcept          = default;
Recipe& Recipe::operator=(Recipe&&) noexcept = default;

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
std::string Recipe::name() const { return impl_->name; }
std::string Recipe::type() const { return impl_->type; }
const std::vector<InspectorConfig>& Recipe::inspector_configs() const {
    return impl_->inspector_configs;
}

ROI Recipe::ref_plane_roi() const {
    return impl_->ref_plane_roi;
}

bool Recipe::has_reference_plane() const {
    return impl_->ref_plane_roi.area() > 0;
}

// ---------------------------------------------------------------------------
// Helper: parse ROI from JSON
// ---------------------------------------------------------------------------
static ROI parse_roi(const json& j) {
    ROI r;
    r.x = j.value("x", 0);
    r.y = j.value("y", 0);
    r.w = j.value("w", 0);
    r.h = j.value("h", 0);
    return r;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
Result<Recipe> Recipe::load(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return Result<Recipe>::failure(ErrorCode::FILE_NOT_FOUND,
                                      "Cannot open recipe file: " + path);
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        return Result<Recipe>::failure(ErrorCode::INVALID_PARAMETER,
                                      std::string("JSON parse error: ") + e.what());
    }

    Recipe recipe;
    auto& impl = *recipe.impl_;

    impl.version     = j.value("version", "1.0");
    impl.name        = j.value("name", "");
    impl.type        = j.value("type", "");
    impl.description = j.value("description", "");
    impl.created     = j.value("created", "");
    impl.modified    = j.value("modified", "");

    // Reference section.
    if (j.contains("reference")) {
        const auto& ref = j["reference"];
        impl.ref_height_map_file = ref.value("height_map_file", "");
        if (ref.contains("ref_plane_roi")) {
            impl.ref_plane_roi = parse_roi(ref["ref_plane_roi"]);
        }
    }

    // 3D inspectors.
    if (j.contains("inspectors_3d")) {
        for (const auto& insp : j["inspectors_3d"]) {
            InspectorConfig cfg;
            cfg.name = insp.value("name", "");
            cfg.type = insp.value("type", "");

            if (insp.contains("rois")) {
                for (const auto& rj : insp["rois"]) {
                    cfg.rois.push_back(parse_roi(rj));
                }
            }

            if (insp.contains("params")) {
                for (auto& [key, val] : insp["params"].items()) {
                    if (val.is_number()) {
                        cfg.params[key] = val.get<double>();
                    }
                    // Skip non-numeric params (e.g. "reference": "board_surface").
                }
            }

            cfg.severity = "critical";  // default; overridden by judgement
            impl.inspector_configs.push_back(std::move(cfg));
        }
    }

    // Judgement rules -> map severity to inspectors.
    if (j.contains("judgement") && j["judgement"].contains("rules")) {
        for (const auto& rule : j["judgement"]["rules"]) {
            std::string insp_name = rule.value("inspector", "");
            std::string sev       = rule.value("severity", "critical");
            impl.severity_map[insp_name] = sev;
        }
        // Apply severity to configs.
        for (auto& cfg : impl.inspector_configs) {
            auto it = impl.severity_map.find(cfg.name);
            if (it != impl.severity_map.end()) {
                cfg.severity = it->second;
            }
        }
    }

    return Result<Recipe>::success(std::move(recipe));
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------
Result<void> Recipe::save(const std::string& path) const {
    json j;
    const auto& impl = *impl_;

    j["version"]     = impl.version;
    j["name"]        = impl.name;
    j["type"]        = impl.type;
    j["description"] = impl.description;
    j["created"]     = impl.created;

    // Update modified timestamp.
    {
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
        j["modified"] = oss.str();
    }

    // Reference.
    if (!impl.ref_height_map_file.empty()) {
        json ref;
        ref["height_map_file"] = impl.ref_height_map_file;
        ref["ref_plane_roi"] = {
            {"x", impl.ref_plane_roi.x},
            {"y", impl.ref_plane_roi.y},
            {"w", impl.ref_plane_roi.w},
            {"h", impl.ref_plane_roi.h}
        };
        j["reference"] = ref;
    }

    // 3D inspectors.
    json inspectors = json::array();
    for (const auto& cfg : impl.inspector_configs) {
        json insp;
        insp["name"] = cfg.name;
        insp["type"] = cfg.type;

        if (!cfg.rois.empty()) {
            json rois = json::array();
            for (const auto& roi : cfg.rois) {
                rois.push_back({{"x", roi.x}, {"y", roi.y},
                                {"w", roi.w}, {"h", roi.h}});
            }
            insp["rois"] = rois;
        }

        if (!cfg.params.empty()) {
            json params;
            for (const auto& [k, v] : cfg.params) {
                params[k] = v;
            }
            insp["params"] = params;
        }

        inspectors.push_back(insp);
    }
    j["inspectors_3d"] = inspectors;
    j["inspectors_2d"] = json::array();

    // Judgement.
    json rules = json::array();
    for (const auto& cfg : impl.inspector_configs) {
        rules.push_back({{"inspector", cfg.name}, {"severity", cfg.severity}});
    }
    j["judgement"] = {{"mode", "all_pass"}, {"rules", rules}};

    // Output defaults.
    j["output"] = {
        {"save_all_images", false},
        {"save_ng_images", true},
        {"save_height_map", false},
        {"log_level", "INFO"}
    };

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return Result<void>::failure(ErrorCode::IO_WRITE_FAILED,
                                    "Cannot write recipe file: " + path);
    }
    ofs << j.dump(2) << "\n";

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------
Result<void> Recipe::validate() const {
    const auto& impl = *impl_;

    if (impl.name.empty()) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Recipe name is empty");
    }

    static const std::set<std::string> known_types = {
        "ref_plane_fit", "height_measure", "flatness",
        "height_threshold", "defect_cluster"
    };

    for (const auto& cfg : impl.inspector_configs) {
        if (known_types.find(cfg.type) == known_types.end()) {
            return Result<void>::failure(
                ErrorCode::INVALID_PARAMETER,
                "Unknown inspector type: " + cfg.type +
                " (inspector: " + cfg.name + ")");
        }

        // Validate ROIs are non-negative.
        for (const auto& roi : cfg.rois) {
            if (roi.x < 0 || roi.y < 0 || roi.w <= 0 || roi.h <= 0) {
                return Result<void>::failure(
                    ErrorCode::INVALID_PARAMETER,
                    "Invalid ROI in inspector: " + cfg.name);
            }
        }

        // Validate severity.
        if (cfg.severity != "critical" &&
            cfg.severity != "warning" &&
            cfg.severity != "minor") {
            return Result<void>::failure(
                ErrorCode::INVALID_PARAMETER,
                "Invalid severity '" + cfg.severity +
                "' in inspector: " + cfg.name);
        }
    }

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// inspect
// ---------------------------------------------------------------------------
Result<InspectionResult> Recipe::inspect(const HeightMap& hmap) const {
    Inspector3D engine;

    for (const auto& cfg : impl_->inspector_configs) {
        engine.add_inspector(cfg);
    }

    auto res = engine.run(hmap);
    if (res.ok()) {
        res.value.recipe_name = impl_->name;
    }
    return res;
}

} // namespace vxl
