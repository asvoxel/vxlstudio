#include "vxl/core.h"

namespace vxl {

const char* version_string() {
    // Constructed at compile time via macros defined in core.h
    static const char version[] =
        "VxlStudio Core "
#define VXL_STRINGIFY_(x) #x
#define VXL_STRINGIFY(x) VXL_STRINGIFY_(x)
        VXL_STRINGIFY(VXL_VERSION_MAJOR) "."
        VXL_STRINGIFY(VXL_VERSION_MINOR) "."
        VXL_STRINGIFY(VXL_VERSION_PATCH);
#undef VXL_STRINGIFY
#undef VXL_STRINGIFY_
    return version;
}

} // namespace vxl
