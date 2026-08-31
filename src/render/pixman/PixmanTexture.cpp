#include "PixmanTexture.hpp"
#include "PixmanFormat.hpp"
#include "../../debug/log/Logger.hpp"
#include "../../helpers/Format.hpp"

#include <cstring>
#include <drm_fourcc.h>

using namespace Render;
using namespace Render::Pixman;

CPixmanTexture::CPixmanTexture(bool opaque) {
    m_opaque = opaque;
}

CPixmanTexture::CPixmanTexture(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const Vector2D& size, bool keepDataCopy, bool opaque) :
    ITexture(drmFormat, pixels, stride, size, keepDataCopy, opaque) {
    m_isSynchronous = true;

    allocate(size, drmFormat);
    if (m_image)
        writePixels(drmFormat, pixels, stride, CBox{{}, size});
}

CPixmanTexture::CPixmanTexture(pixman_image_t* image) {
    if (!image)
        return;

    m_image     = pixman_image_ref(image);
    m_size      = {pixman_image_get_width(image), pixman_image_get_height(image)};
    m_drmFormat = drmFormatFromPixman(pixman_image_get_format(image));
    m_stride    = pixman_image_get_stride(image);
}

CPixmanTexture::~CPixmanTexture() {
    destroyImage();
}

void CPixmanTexture::destroyImage() {
    if (m_image) {
        pixman_image_unref(m_image);
        m_image = nullptr;
    }
}

void CPixmanTexture::allocate(const Vector2D& size, uint32_t drmFormat) {
    if (drmFormat == 0)
        drmFormat = DRM_FORMAT_ARGB8888;

    if (m_image && m_size == size && m_drmFormat == drmFormat)
        return;

    destroyImage();

    const auto PIXFMT = pixmanFormatFromDRM(drmFormat);
    if (!PIXFMT) {
        Log::logger->log(Log::ERR, "pixman: cannot allocate a texture with format {}, no pixman equivalent", NFormatUtils::drmFormatName(drmFormat));
        return;
    }

    m_size      = size;
    m_drmFormat = drmFormat;
    m_stride    = sc<uint32_t>(size.x) * (PIXMAN_FORMAT_BPP(PIXFMT) / 8);

    m_pixels.assign(sc<size_t>(m_stride) * sc<size_t>(size.y), 0);
    m_image = pixman_image_create_bits_no_clear(PIXFMT, size.x, size.y, rc<uint32_t*>(m_pixels.data()), m_stride);

    if (!m_image) {
        Log::logger->log(Log::ERR, "pixman: pixman_image_create_bits failed for a {} texture", size);
        m_pixels.clear();
    }
}

bool CPixmanTexture::writePixels(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const CRegion& damage) {
    if (!m_image || !pixels)
        return false;

    if (drmFormat != m_drmFormat) {
        Log::logger->log(Log::ERR, "pixman: texture update with a mismatched format ({} vs {})", NFormatUtils::drmFormatName(drmFormat),
                         NFormatUtils::drmFormatName(m_drmFormat));
        return false;
    }

    const auto   BPP     = pixman_image_get_depth(m_image) == 0 ? 4 : PIXMAN_FORMAT_BPP(pixman_image_get_format(m_image)) / 8;
    const size_t MAXROWS = sc<size_t>(m_size.y);

    // copy only the damaged rows: the client buffer is released right after commit,
    // so unlike wlroots we cannot wrap the client mapping.
    damage.copy().intersect(CBox{{}, m_size}).forEachRect([&](const auto& rect) {
        const size_t X = std::max(rect.x1, 0);
        const size_t W = std::min(sc<size_t>(rect.x2), sc<size_t>(m_size.x)) - X;
        for (size_t y = std::max(rect.y1, 0); y < std::min(sc<size_t>(rect.y2), MAXROWS); ++y) {
            std::memcpy(m_pixels.data() + y * m_stride + X * BPP, pixels + y * stride + X * BPP, W * BPP);
        }
    });

    return true;
}

void CPixmanTexture::update(uint32_t drmFormat, uint8_t* pixels, uint32_t stride, const CRegion& damage) {
    if (damage.empty())
        return;

    if (!m_image)
        allocate(m_size, drmFormat);

    writePixels(drmFormat, pixels, stride, damage);

    if (m_keepDataCopy) {
        m_dataCopy.resize(sc<size_t>(stride) * sc<size_t>(m_size.y));
        memcpy(m_dataCopy.data(), pixels, sc<size_t>(stride) * sc<size_t>(m_size.y));
    }
}

void CPixmanTexture::setTexParameter(GLenum pname, GLint param) {
    ; // no-op: GL-only concept, pixman filtering is chosen per draw
}

bool CPixmanTexture::ok() {
    return !!m_image;
}

pixman_image_t* CPixmanTexture::image() {
    return m_image;
}
