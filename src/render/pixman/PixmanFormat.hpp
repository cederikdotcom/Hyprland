#pragma once

#include <cstdint>
#include <pixman.h>

namespace Render::Pixman {
    // returns 0 for formats with no pixman equivalent
    pixman_format_code_t pixmanFormatFromDRM(uint32_t drmFormat);
    uint32_t             drmFormatFromPixman(pixman_format_code_t format);
}
