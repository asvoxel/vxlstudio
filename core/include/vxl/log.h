#pragma once

#include <functional>
#include <string>

#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {
namespace log {

enum class Level : int {
    TRACE = 0,
    DEBUG = 1,
    INFO  = 2,
    WARN  = 3,
    ERROR = 4,
    FATAL = 5
};

VXL_EXPORT void init();

VXL_EXPORT void set_level(Level level);

VXL_EXPORT void trace(const std::string& msg);
VXL_EXPORT void debug(const std::string& msg);
VXL_EXPORT void info(const std::string& msg);
VXL_EXPORT void warn(const std::string& msg);
VXL_EXPORT void error(const std::string& msg);
VXL_EXPORT void fatal(const std::string& msg);

VXL_EXPORT void add_console_sink();
VXL_EXPORT void add_file_sink(const std::string& path);
VXL_EXPORT void add_callback_sink(std::function<void(Level, const std::string&)> callback);

VXL_EXPORT void save_image(const Image& img, const std::string& tag);
VXL_EXPORT void save_height_map(const HeightMap& hm, const std::string& tag);
VXL_EXPORT void save_result(const InspectionResult& result);

VXL_EXPORT void set_log_dir(const std::string& dir);
VXL_EXPORT void set_max_days(int days);
VXL_EXPORT void set_max_size_mb(int size_mb);

} // namespace log
} // namespace vxl
