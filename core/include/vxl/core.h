#pragma once

// VxlStudio Core -- master include
// Include this header to get all public VxlStudio Core types and utilities.

#include "vxl/export.h"
#include "vxl/error.h"
#include "vxl/types.h"
#include "vxl/log.h"
#include "vxl/message_bus.h"
#include "vxl/camera.h"
#include "vxl/reconstruct.h"
#include "vxl/calibration.h"
#include "vxl/height_map.h"
#include "vxl/point_cloud.h"
#include "vxl/inspector_3d.h"
#include "vxl/inspector_2d.h"
#include "vxl/inference.h"
#include "vxl/result.h"
#include "vxl/recipe.h"
#include "vxl/device.h"
#include "vxl/io.h"

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------
#define VXL_VERSION_MAJOR 0
#define VXL_VERSION_MINOR 1
#define VXL_VERSION_PATCH 0

namespace vxl {

VXL_EXPORT const char* version_string();

} // namespace vxl
