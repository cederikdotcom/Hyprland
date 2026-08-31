#include "PixmanFramebuffer.hpp"
#include "PixmanFormat.hpp"
#include "../Renderer.hpp"
#include "../../debug/log/Logger.hpp"
#include "../../helpers/Format.hpp"
#include "../../protocols/types/Buffer.hpp"

#include <cstring>

using namespace Render;
using namespace Render::Pixman;

CPixmanFramebuffer::CPixmanFramebuffer() : IFramebuffer() {}
CPixmanFramebuffer::CPixmanFramebuffer(const std::string& name) : IFramebuffer(name) {}

CPixmanFramebuffer::~CPixmanFramebuffer() {
    release();
}

bool CPixmanFramebuffer::internalAlloc(int w, int h, DRMFormat format) {
    const auto PIXFMT = pixmanFormatFromDRM(format);
    if (!PIXFMT) {
        Log::logger->log(Log::ERR, "pixman: cannot alloc fb \"{}\": format {} has no pixman equivalent", m_name, NFormatUtils::drmFormatName(format));
        return false;
    }

    if (m_image) {
        pixman_image_unref(m_image);
        m_image = nullptr;
    }
    m_external = false;

    const uint32_t STRIDE = sc<uint32_t>(w) * (PIXMAN_FORMAT_BPP(PIXFMT) / 8);
    m_pixels.assign(sc<size_t>(STRIDE) * sc<size_t>(h), 0);
    m_image = pixman_image_create_bits_no_clear(PIXFMT, w, h, rc<uint32_t*>(m_pixels.data()), STRIDE);

    if (!m_image) {
        Log::logger->log(Log::ERR, "pixman: fb \"{}\": pixman_image_create_bits failed ({}x{})", m_name, w, h);
        m_pixels.clear();
        return false;
    }

    m_tex = makeShared<CPixmanTexture>(m_image);

    Log::logger->log(Log::DEBUG, "pixman: framebuffer \"{}\" created, {}x{}", m_name, w, h);

    return true;
}

bool CPixmanFramebuffer::wrapExternal(uint8_t* data, uint32_t drmFormat, const Vector2D& size, uint32_t stride) {
    const auto PIXFMT = pixmanFormatFromDRM(drmFormat);
    if (!PIXFMT) {
        Log::logger->log(Log::ERR, "pixman: cannot wrap a buffer with format {}, no pixman equivalent", NFormatUtils::drmFormatName(drmFormat));
        return false;
    }

    if (m_image) {
        pixman_image_unref(m_image);
        m_image = nullptr;
        m_tex.reset();
    }

    m_pixels.clear();
    m_image = pixman_image_create_bits_no_clear(PIXFMT, size.x, size.y, rc<uint32_t*>(data), stride);

    if (!m_image) {
        Log::logger->log(Log::ERR, "pixman: pixman_image_create_bits failed wrapping a {} buffer", size);
        return false;
    }

    m_external    = true;
    m_size        = size;
    m_drmFormat   = drmFormat;
    m_fbAllocated = true;
    m_tex         = makeShared<CPixmanTexture>(m_image);

    return true;
}

void CPixmanFramebuffer::unwrapExternal() {
    if (!m_external)
        return;

    release();
}

void CPixmanFramebuffer::addStencil(SP<ITexture> tex) {
    ; // no-op: stencil is only used by rounding / blur, dead in flat mode
}

void CPixmanFramebuffer::bind() {
    ; // the pixman renderer switches its target image in bindFB()
}

void CPixmanFramebuffer::release() {
    if (m_image) {
        pixman_image_unref(m_image);
        m_image = nullptr;
    }

    m_tex.reset();
    m_pixels.clear();
    m_external    = false;
    m_fbAllocated = false;
    m_size        = Vector2D();
}

pixman_image_t* CPixmanFramebuffer::image() {
    return m_image;
}

bool CPixmanFramebuffer::readPixels(CHLBufferReference buffer, uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height) {
    if (!m_image) {
        Log::logger->log(Log::ERR, "pixman: readPixels on an unallocated fb");
        return false;
    }

    auto shm = buffer->shm();
    if (!shm.success) {
        Log::logger->log(Log::ERR, "pixman: can't copy: buffer is not shm");
        return false;
    }

    auto [pixelData, fmt, bufLen] = buffer->beginDataPtr(0); // no need for end, cuz it's shm
    if (!pixelData) {
        Log::logger->log(Log::ERR, "pixman: can't copy: failed to get shm data pointer");
        return false;
    }

    const auto PIXFMT = pixmanFormatFromDRM(shm.format);
    if (!PIXFMT) {
        Log::logger->log(Log::ERR, "pixman: can't copy: no pixman format for {}", NFormatUtils::drmFormatName(shm.format));
        return false;
    }

    const auto readWidth  = width > 0 ? width : sc<uint32_t>(m_size.x);
    const auto readHeight = height > 0 ? height : sc<uint32_t>(m_size.y);

    if (offsetX + readWidth > sc<uint32_t>(m_size.x) || offsetY + readHeight > sc<uint32_t>(m_size.y) || offsetX + readWidth > sc<uint32_t>(shm.size.x) ||
        offsetY + readHeight > sc<uint32_t>(shm.size.y)) {
        Log::logger->log(Log::ERR, "pixman: can't copy: read rect exceeds fb or shm bounds");
        return false;
    }

    auto dstImage = pixman_image_create_bits_no_clear(PIXFMT, shm.size.x, shm.size.y, rc<uint32_t*>(pixelData), shm.stride);
    if (!dstImage) {
        Log::logger->log(Log::ERR, "pixman: can't copy: failed to wrap the dst shm buffer");
        return false;
    }

    // pixman converts formats for us
    pixman_image_composite32(PIXMAN_OP_SRC, m_image, nullptr, dstImage, offsetX, offsetY, 0, 0, offsetX, offsetY, readWidth, readHeight);
    pixman_image_unref(dstImage);

    return true;
}
