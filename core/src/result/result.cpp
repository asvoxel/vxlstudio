#include "vxl/result.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vxl {

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

void build_inspection_result(InspectionResult& out,
                              const std::vector<InspectorResult>& per_inspector,
                              const std::string& recipe_name) {
    out.ok = true;
    out.recipe_name = recipe_name;
    out.timestamp = now_iso8601();
    out.defects.clear();
    out.measures.clear();

    for (const auto& ir : per_inspector) {
        if (!ir.pass && ir.severity == "critical") {
            out.ok = false;
        }
        // Aggregate measures.
        if (ir.measure.min_height != 0.0f || ir.measure.max_height != 0.0f ||
            ir.measure.avg_height != 0.0f) {
            out.measures.push_back(ir.measure);
        }
        // Aggregate defects.
        for (const auto& d : ir.defects) {
            out.defects.push_back(d);
        }
    }
}

} // namespace vxl
