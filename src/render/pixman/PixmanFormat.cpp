#include "PixmanFormat.hpp"
#include "../../macros.hpp"

#include <array>
#include <drm_fourcc.h>

using namespace Render::Pixman;

namespace {
    struct SPixmanDRMFormat {
        uint32_t             drmFormat;
        pixman_format_code_t pixmanFormat;
    };

    // cribbed from wlroots render/pixman/pixel_format.c, little-endian only
    constexpr std::array<SPixmanDRMFormat, 15> PIXMAN_FORMATS = {{
        {DRM_FORMAT_ARGB8888, PIXMAN_a8r8g8b8},
        {DRM_FORMAT_XRGB8888, PIXMAN_x8r8g8b8},
        {DRM_FORMAT_ABGR8888, PIXMAN_a8b8g8r8},
        {DRM_FORMAT_XBGR8888, PIXMAN_x8b8g8r8},
        {DRM_FORMAT_RGBA8888, PIXMAN_r8g8b8a8},
        {DRM_FORMAT_RGBX8888, PIXMAN_r8g8b8x8},
        {DRM_FORMAT_BGRA8888, PIXMAN_b8g8r8a8},
        {DRM_FORMAT_BGRX8888, PIXMAN_b8g8r8x8},
        {DRM_FORMAT_RGB565, PIXMAN_r5g6b5},
        {DRM_FORMAT_BGR565, PIXMAN_b5g6r5},
        {DRM_FORMAT_ARGB2101010, PIXMAN_a2r10g10b10},
        {DRM_FORMAT_XRGB2101010, PIXMAN_x2r10g10b10},
        {DRM_FORMAT_ABGR2101010, PIXMAN_a2b10g10r10},
        {DRM_FORMAT_XBGR2101010, PIXMAN_x2b10g10r10},
        {DRM_FORMAT_ABGR16161616, PIXMAN_a16b16g16r16},
    }};
}

pixman_format_code_t Render::Pixman::pixmanFormatFromDRM(uint32_t drmFormat) {
    for (const auto& fmt : PIXMAN_FORMATS) {
        if (fmt.drmFormat == drmFormat)
            return fmt.pixmanFormat;
    }

    return sc<pixman_format_code_t>(0);
}

uint32_t Render::Pixman::drmFormatFromPixman(pixman_format_code_t format) {
    for (const auto& fmt : PIXMAN_FORMATS) {
        if (fmt.pixmanFormat == format)
            return fmt.drmFormat;
    }

    return DRM_FORMAT_INVALID;
}
