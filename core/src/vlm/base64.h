// VxlStudio -- base64 encoder for VLM image payloads
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vxl {
namespace detail {

// Encode raw bytes to base64 string
std::string base64_encode(const uint8_t* data, size_t length);

} // namespace detail
} // namespace vxl
