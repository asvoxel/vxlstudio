// VxlStudio -- base64 encoder implementation
// SPDX-License-Identifier: MIT

#include "base64.h"

namespace vxl {
namespace detail {

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t length) {
    std::string result;
    result.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t octet_a = data[i];
        uint32_t octet_b = (i + 1 < length) ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2 < length) ? data[i + 2] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result += kBase64Table[(triple >> 18) & 0x3F];
        result += kBase64Table[(triple >> 12) & 0x3F];
        result += (i + 1 < length) ? kBase64Table[(triple >> 6) & 0x3F] : '=';
        result += (i + 2 < length) ? kBase64Table[triple & 0x3F] : '=';
    }

    return result;
}

} // namespace detail
} // namespace vxl
